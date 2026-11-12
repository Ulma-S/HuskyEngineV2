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

        // ハンドル1つを、番号(Int)か null にする。
        ArchiveValue HandleToValue(core::ObjectHandle handle, const HandleToId& toId)
        {
            const std::int64_t id = handle.IsNull() ? -1 : toId(handle);
            return id >= 0 ? ArchiveValue(id) : ArchiveValue();
        }

        // プロパティ1つの値を ArchiveValue にする。書かないもの(対応表の無いハンドル)なら Null を返し、ok を false にする。
        ArchiveValue PropertyToValue(const Property& property, const core::Object& object, const HandleToId& toId, bool& ok)
        {
            ok = true;
            // std::function は、中身が入っているかを if で確かめられる。入っていなければハンドルは書かない。
            if (property.IsReference() && !toId)
            {
                ok = false;
                return ArchiveValue();
            }
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
                return HandleToValue(*property.GetValuePtr<core::ObjectHandle>(object), toId);
            case PropertyType::ObjectHandleArray:
            {
                // 番号の配列にする。番号の無い相手(保存しないもの、もう無いもの)は書かない。
                ArchiveValue array = ArchiveValue::MakeArray();
                for (const core::ObjectHandle handle : *property.GetValuePtr<std::vector<core::ObjectHandle>>(object))
                {
                    ArchiveValue element = HandleToValue(handle, toId);
                    if (!element.IsNull())
                    {
                        array.Append(std::move(element));
                    }
                }
                return array;
            }
            }
            ok = false;
            return ArchiveValue();
        }

        // 番号(Int)か null を、ハンドルにする。読めなければ理由を返す(空なら成功)。
        std::string ValueToHandle(const ArchiveValue& value, const IdToHandle& toHandle, core::ObjectHandle& outHandle)
        {
            if (value.IsNull())
            {
                outHandle = core::ObjectHandle{}; // どこも指さない
                return {};
            }
            if (value.GetType() != ArchiveValue::Type::Int)
            {
                return "番号(整数)ではありません";
            }
            outHandle = toHandle(value.AsInt());
            if (outHandle.IsNull())
            {
                return "番号 " + std::to_string(value.AsInt()) + " のオブジェクトがありません";
            }
            return {};
        }

        // ArchiveValue の値を、プロパティに書き込む。型が合わなければ書き込まず、理由を返す(空なら成功)。
        std::string ValueToProperty(const ArchiveValue& value, const Property& property, core::Object& object,
                                    const IdToHandle& toHandle)
        {
            if (property.IsReference() && !toHandle)
            {
                return "ハンドルを読むには、番号の対応表が要ります";
            }
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
            {
                core::ObjectHandle handle;
                std::string error = ValueToHandle(value, toHandle, handle);
                if (error.empty())
                {
                    *property.GetValuePtr<core::ObjectHandle>(object) = handle;
                }
                return error;
            }
            case PropertyType::ObjectHandleArray:
            {
                if (value.GetType() != ArchiveValue::Type::Array)
                {
                    return "配列ではありません";
                }
                // 読めた要素だけを並べる。読めなかった要素があれば、最初の理由を返す(読めたものは書き込む)。
                std::vector<core::ObjectHandle> handles;
                std::string firstError;
                for (std::size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    core::ObjectHandle handle;
                    std::string error = ValueToHandle(value.At(i), toHandle, handle);
                    if (error.empty() && !handle.IsNull())
                    {
                        handles.push_back(handle);
                    }
                    else if (!error.empty() && firstError.empty())
                    {
                        firstError = std::move(error);
                    }
                }
                *property.GetValuePtr<std::vector<core::ObjectHandle>>(object) = std::move(handles);
                return firstError;
            }
            }
            return "知らない種類のプロパティです";
        }
    }

    ArchiveValue Write(const core::Object& object, const HandleToId& toId)
    {
        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("type", ArchiveValue(object.GetType().GetName()));
        ArchiveValue& properties = root.Set("properties", ArchiveValue::MakeObject());

        // ForEachProperty は、親クラスのプロパティから順に渡す(Object → Actor → Enemy の順)。
        object.GetType().ForEachProperty([&](const Property& property) {
            bool ok = false;
            ArchiveValue value = PropertyToValue(property, object, toId, ok);
            if (ok)
            {
                properties.Set(property.GetName(), std::move(value));
            }
        });
        return root;
    }

    ReadReport ReadProperties(const ArchiveValue& value, core::Object& object, const IdToHandle& toHandle)
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
            const std::string error = ValueToProperty(properties->GetMemberValue(i), *property, object, toHandle);
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
