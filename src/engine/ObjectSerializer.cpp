// ObjectSerializer.h の実装。
#include "engine/ObjectSerializer.h"

#include "core/Property.h"
#include "core/TypeInfo.h"

#include <cstdint>
#include <limits>
#include <utility> // std::move

namespace husky::engine::ObjectSerializer
{
    namespace
    {
        using core::Property;
        using core::PropertyType;

        // プロパティ1つの値を ArchiveValue にする。書かないもの(ハンドル)なら Null を返し、ok を false にする。
        ArchiveValue PropertyToValue(const Property& property, const core::Object& object, bool& ok)
        {
            ok = true;
            switch (property.GetType())
            {
            case PropertyType::Bool:
                return ArchiveValue(*property.GetValuePtr<bool>(object));
            case PropertyType::Int32:
                return ArchiveValue(static_cast<std::int64_t>(*property.GetValuePtr<std::int32_t>(object)));
            case PropertyType::Float:
                return ArchiveValue(*property.GetValuePtr<float>(object)); // float の精度の印が付く
            case PropertyType::String:
                return ArchiveValue(*property.GetValuePtr<std::string>(object));
            case PropertyType::ObjectHandle:
            case PropertyType::ObjectHandleArray:
                break; // ハンドルは J-3 で扱う
            }
            ok = false;
            return ArchiveValue();
        }

        // ArchiveValue の値を、プロパティに書き込む。型が合わなければ書き込まず、理由を返す(空なら成功)。
        std::string ValueToProperty(const ArchiveValue& value, const Property& property, core::Object& object)
        {
            switch (property.GetType())
            {
            case PropertyType::Bool:
                if (value.GetType() != ArchiveValue::Type::Bool)
                {
                    return "bool ではありません";
                }
                *property.GetValuePtr<bool>(object) = value.AsBool();
                return {};
            case PropertyType::Int32:
            {
                if (value.GetType() != ArchiveValue::Type::Int)
                {
                    return "整数ではありません";
                }
                const std::int64_t number = value.AsInt();
                if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max())
                {
                    return "int32 の範囲を超えています";
                }
                *property.GetValuePtr<std::int32_t>(object) = static_cast<std::int32_t>(number);
                return {};
            }
            case PropertyType::Float:
                // 整数で書かれていても読める(JSON を手で書くと 1.0 を 1 と書きがちなため)。
                if (value.GetType() != ArchiveValue::Type::Float && value.GetType() != ArchiveValue::Type::Int)
                {
                    return "数ではありません";
                }
                *property.GetValuePtr<float>(object) = static_cast<float>(value.AsFloat());
                return {};
            case PropertyType::String:
                if (value.GetType() != ArchiveValue::Type::String)
                {
                    return "文字列ではありません";
                }
                *property.GetValuePtr<std::string>(object) = value.AsString();
                return {};
            case PropertyType::ObjectHandle:
            case PropertyType::ObjectHandleArray:
                return "ハンドルの読み込みにはまだ対応していません";
            }
            return "知らない種類のプロパティです";
        }
    }

    ArchiveValue Write(const core::Object& object)
    {
        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("type", ArchiveValue(object.GetType().GetName()));
        ArchiveValue& properties = root.Set("properties", ArchiveValue::MakeObject());

        // ForEachProperty は、親クラスのプロパティから順に渡す(Object → Actor → Enemy の順)。
        object.GetType().ForEachProperty([&](const Property& property) {
            bool ok = false;
            ArchiveValue value = PropertyToValue(property, object, ok);
            if (ok)
            {
                properties.Set(property.GetName(), std::move(value));
            }
        });
        return root;
    }

    ReadReport ReadProperties(const ArchiveValue& value, core::Object& object)
    {
        ReadReport report;
        const core::TypeInfo& type = object.GetType();

        const ArchiveValue* typeName = value.Find("type");
        if (typeName != nullptr && typeName->AsString() != type.GetName())
        {
            report.warnings.push_back("型が違います(保存: " + typeName->AsString() + "、読み込み先: " + type.GetName() + ")");
        }

        const ArchiveValue* properties = value.Find("properties");
        if (properties == nullptr || properties->GetType() != ArchiveValue::Type::Object)
        {
            report.warnings.push_back("\"properties\" がありません");
            return report;
        }

        for (std::size_t i = 0; i < properties->GetMemberCount(); ++i)
        {
            const std::string& name = properties->GetMemberName(i);
            const core::Property* property = type.FindProperty(name.c_str());
            if (property == nullptr)
            {
                report.warnings.push_back(name + ": この型にはありません");
                continue;
            }
            const std::string error = ValueToProperty(properties->GetMemberValue(i), *property, object);
            if (error.empty())
            {
                ++report.readCount;
            }
            else
            {
                report.warnings.push_back(name + ": " + error);
            }
        }
        return report;
    }
}
