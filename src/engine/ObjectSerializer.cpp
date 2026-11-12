// ObjectSerializer.h の実装。
// 値の読み書きは、必ず Property の GetValuePtrAt / GetStructAddressAt を通す(オフセットを直接触らない)。
// container は「プロパティの持ち主(オブジェクトか構造体)の先頭のアドレス」。構造体の中に入るときは、構造体のアドレスに替える。
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

        // 下で定義する関数を、先に宣言しておく(PropertyToValue と PropertiesToValue は、お互いを呼び合うため)。
        ArchiveValue PropertiesToValue(const core::TypeInfo& type, const void* container, const HandleToId& toId);

        // プロパティ1つの値を ArchiveValue にする。書かないもの(対応表の無いハンドル)なら Null を返し、ok を false にする。
        ArchiveValue PropertyToValue(const Property& property, const void* container, const HandleToId& toId, bool& ok)
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
                return ArchiveValue(*property.GetValuePtrAt<bool>(container));
            case PropertyType::Int32:
                return ArchiveValue(static_cast<std::int64_t>(*property.GetValuePtrAt<std::int32_t>(container)));
            case PropertyType::Float:
                return ArchiveValue(*property.GetValuePtrAt<float>(container)); // float の精度の印が付く
            case PropertyType::String:
                return ArchiveValue(*property.GetValuePtrAt<std::string>(container));
            case PropertyType::ObjectHandle:
                return HandleToValue(*property.GetValuePtrAt<core::ObjectHandle>(container), toId);
            case PropertyType::ObjectHandleArray:
            {
                // 番号の配列にする。番号の無い相手(保存しないもの、もう無いもの)は書かない。
                ArchiveValue array = ArchiveValue::MakeArray();
                for (const core::ObjectHandle handle : *property.GetValuePtrAt<std::vector<core::ObjectHandle>>(container))
                {
                    ArchiveValue element = HandleToValue(handle, toId);
                    if (!element.IsNull())
                    {
                        array.Append(std::move(element));
                    }
                }
                return array;
            }
            case PropertyType::Struct:
                // 構造体: 中のメンバーを、入れ子のオブジェクトとして書く(オブジェクトのプロパティと同じ書き方を繰り返す)。
                return PropertiesToValue(*property.GetStructType(), property.GetStructAddressAt(container), toId);
            case PropertyType::AssetRef:
            {
                // アセットの参照: GUID の文字列だけを書く(ハンドルは実行中だけの値なので書かない)。何も指さなければ null。
                const core::AssetReference& assetRef = *property.GetValuePtrAt<core::AssetReference>(container);
                return assetRef.IsNull() ? ArchiveValue() : ArchiveValue(assetRef.guid.ToString());
            }
            }
            ok = false;
            return ArchiveValue();
        }

        // 型の全プロパティ(親の型のものから順に)を、「名前 → 値」のオブジェクトにする。
        ArchiveValue PropertiesToValue(const core::TypeInfo& type, const void* container, const HandleToId& toId)
        {
            ArchiveValue properties = ArchiveValue::MakeObject();
            type.ForEachProperty([&](const Property& property) {
                bool ok = false;
                ArchiveValue value = PropertyToValue(property, container, toId, ok);
                if (ok)
                {
                    properties.Set(property.GetName(), std::move(value));
                }
            });
            return properties;
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

        // ArchiveValue の値を、プロパティ(構造体以外)に書き込む。型が合わなければ書き込まず、理由を返す(空なら成功)。
        std::string ValueToProperty(const ArchiveValue& value, const Property& property, void* container,
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
                *property.GetValuePtrAt<bool>(container) = value.AsBool();
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
                *property.GetValuePtrAt<std::int32_t>(container) = static_cast<std::int32_t>(number);
                return {};
            }
            case PropertyType::Float:
                // 整数で書かれていても読める(JSON を手で書くと 1.0 を 1 と書きがちなため)。
                if (value.GetType() != ArchiveValue::Type::Float && value.GetType() != ArchiveValue::Type::Int)
                {
                    return "数ではありません";
                }
                *property.GetValuePtrAt<float>(container) = static_cast<float>(value.AsFloat());
                return {};
            case PropertyType::String:
                if (value.GetType() != ArchiveValue::Type::String)
                {
                    return "文字列ではありません";
                }
                *property.GetValuePtrAt<std::string>(container) = value.AsString();
                return {};
            case PropertyType::ObjectHandle:
            {
                core::ObjectHandle handle;
                std::string error = ValueToHandle(value, toHandle, handle);
                if (error.empty())
                {
                    *property.GetValuePtrAt<core::ObjectHandle>(container) = handle;
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
                *property.GetValuePtrAt<std::vector<core::ObjectHandle>>(container) = std::move(handles);
                return firstError;
            }
            case PropertyType::Struct:
                break; // 構造体は ReadMembers が中に入って読む
            case PropertyType::AssetRef:
            {
                // GUID の文字列(null なら何も指さない)。アセットを読むのは、使うとき(AssetLoader::Resolve)。
                core::Guid guid;
                if (!value.IsNull()
                    && (value.GetType() != ArchiveValue::Type::String || !core::Guid::Parse(value.AsString(), guid)))
                {
                    return "GUID の文字列ではありません";
                }
                core::AssetReference& assetRef = *property.GetValuePtrAt<core::AssetReference>(container);
                assetRef.guid = guid;
                assetRef.asset = {}; // 前に読んでいたアセットは、指す先が変わったので手放す
                return {};
            }
            }
            return "知らない種類のプロパティです";
        }

        // 「名前 → 値」のオブジェクト(members)を、型 type の持ち主(container)のプロパティに書き込む。
        // 構造体のプロパティは、中に入って同じことを繰り返す。warnings の名前には「color.r」のように道筋を付ける。
        void ReadMembers(const ArchiveValue& members, const core::TypeInfo& type, void* container,
                         const IdToHandle& toHandle, const std::string& prefix, ReadReport& report)
        {
            for (std::size_t i = 0; i < members.GetMemberCount(); ++i)
            {
                const std::string name = prefix + members.GetMemberName(i);
                const ArchiveValue& value = members.GetMemberValue(i);
                const core::Property* property = type.FindProperty(members.GetMemberName(i).c_str());
                if (property == nullptr)
                {
                    report.warnings.push_back(name + ": この型にはありません");
                    continue;
                }
                if (property->GetType() == PropertyType::Struct)
                {
                    if (value.GetType() != ArchiveValue::Type::Object)
                    {
                        report.warnings.push_back(name + ": 構造体(オブジェクト)ではありません");
                        continue;
                    }
                    ReadMembers(value, *property->GetStructType(), property->GetStructAddressAt(container), toHandle,
                                name + ".", report);
                    continue;
                }
                const std::string error = ValueToProperty(value, *property, container, toHandle);
                if (error.empty())
                {
                    ++report.readCount;
                }
                else
                {
                    report.warnings.push_back(name + ": " + error);
                }
            }
        }
    }

    ArchiveValue Write(const core::Object& object, const HandleToId& toId)
    {
        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("type", ArchiveValue(object.GetType().GetName()));
        // ForEachProperty は、親クラスのプロパティから順に渡す(Object → Actor → Enemy の順)。
        root.Set("properties", PropertiesToValue(object.GetType(), &object, toId));
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

        ReadMembers(*properties, type, &object, toHandle, "", report);
        return report;
    }
}
