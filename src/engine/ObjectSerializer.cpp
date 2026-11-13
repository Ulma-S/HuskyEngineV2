// ObjectSerializer.h の実装。
// 値の読み書きは、必ず Property の GetValuePtrAt / GetStructAddressAt を通す(オフセットを直接触らない)。
// container は「プロパティの持ち主(オブジェクトか構造体)の先頭のアドレス」。構造体の中に入るときは、構造体のアドレスに替える。
#include "engine/ObjectSerializer.h"

#include "core/ObjectRegistry.h" // ObjectRef の相手の型を確かめる
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
            case PropertyType::Int64:
                return ArchiveValue(*property.GetValuePtrAt<std::int64_t>(container));
            case PropertyType::Double:
                return ArchiveValue(*property.GetValuePtrAt<double>(container)); // double はそのまま(精度の印は付かない)
            case PropertyType::String:
                return ArchiveValue(*property.GetValuePtrAt<std::string>(container));
            case PropertyType::ObjectHandle:
                return HandleToValue(*property.GetValuePtrAt<core::ObjectHandle>(container), toId);
            case PropertyType::ObjectRef: // 型の付いた参照も、中のハンドルを同じ番号で書く
                return HandleToValue(property.GetValuePtrAt<core::ObjectReference>(container)->handle, toId);
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
            case PropertyType::Array:
            {
                // 配列(SF-2): 要素を1つずつ、要素のプロパティで書く(構造体の要素は入れ子のオブジェクトになる)。
                // 書けない要素(番号の無い相手を指す参照など)は null にして、並びの位置は変えない。
                const core::ArrayInfo& array = *property.GetArrayInfo();
                const void* address = property.GetArrayAddressAt(container);
                ArchiveValue values = ArchiveValue::MakeArray();
                const std::size_t count = array.size(address);
                for (std::size_t i = 0; i < count; ++i)
                {
                    bool elementOk = true;
                    if (array.IsProxy())
                    {
                        // 要素の場所が取れない配列(std::vector<bool>): 手元の変数に写してから書く
                        bool temporary = false;
                        array.load(address, i, &temporary);
                        values.Append(PropertyToValue(array.element, &temporary, toId, elementOk));
                        continue;
                    }
                    values.Append(PropertyToValue(array.element, array.atConst(address, i), toId, elementOk));
                }
                return values;
            }
            case PropertyType::AssetRef:
            {
                // アセットの参照: GUID の文字列だけを書く(ハンドルは実行中だけの値なので書かない)。何も指さなければ null。
                const core::AssetReference& assetRef = *property.GetValuePtrAt<core::AssetReference>(container);
                return assetRef.IsNull() ? ArchiveValue() : ArchiveValue(assetRef.guid.ToString());
            }
            case PropertyType::Guid:
            {
                // 変わらない ID: GUID の文字列(アセットの参照と同じ書き方)。まだ振られていなければ null。
                const core::Guid& guid = *property.GetValuePtrAt<core::Guid>(container);
                return guid.IsNull() ? ArchiveValue() : ArchiveValue(guid.ToString());
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

        // 下で定義する(配列の構造体の要素を読むのに、ValueToProperty からも呼ぶため)。
        void ReadMembers(const ArchiveValue& members, const core::TypeInfo& type, void* container,
                         const IdToHandle& toHandle, const core::ObjectRegistry* registry, const std::string& prefix,
                         ReadReport& report);

        // ArchiveValue の値を、プロパティ(構造体以外)に書き込む。型が合わなければ書き込まず、理由を返す(空なら成功)。
        // registry: 型の付いた参照(ObjectRef)の相手の型を確かめるのに使う(nullptr なら確かめない)。
        std::string ValueToProperty(const ArchiveValue& value, const Property& property, void* container,
                                    const IdToHandle& toHandle, const core::ObjectRegistry* registry)
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
            case PropertyType::Int64:
                if (value.GetType() != ArchiveValue::Type::Int)
                {
                    return "整数ではありません";
                }
                *property.GetValuePtrAt<std::int64_t>(container) = value.AsInt();
                return {};
            case PropertyType::Double:
                // float と同じく、整数で書かれていても読める。
                if (value.GetType() != ArchiveValue::Type::Float && value.GetType() != ArchiveValue::Type::Int)
                {
                    return "数ではありません";
                }
                *property.GetValuePtrAt<double>(container) = value.AsFloat();
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
            case PropertyType::Array:
            {
                // 配列(SF-2): 配列まるごとを入れ替える(前の要素は消し、要素は既定の値から読む)。
                // 読めない要素があっても、ほかの要素は読む(その要素は既定の値のまま)。最初の理由を返す。
                if (value.GetType() != ArchiveValue::Type::Array)
                {
                    return "配列ではありません";
                }
                const core::ArrayInfo& array = *property.GetArrayInfo();
                void* address = property.GetArrayAddressAt(container);
                array.resize(address, 0);
                array.resize(address, value.GetArraySize());
                std::string firstError;
                for (std::size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    std::string error;
                    if (array.IsProxy())
                    {
                        // 要素の場所が取れない配列(std::vector<bool>): 手元の変数に読んでから、i 番目に書き戻す
                        bool temporary = false;
                        error = ValueToProperty(value.At(i), array.element, &temporary, toHandle, registry);
                        array.store(address, i, &temporary);
                    }
                    else if (array.element.GetType() == PropertyType::Struct)
                    {
                        // 構造体の要素: 中のメンバーを、構造体のプロパティと同じく ReadMembers で読む
                        if (value.At(i).GetType() != ArchiveValue::Type::Object)
                        {
                            error = "構造体(オブジェクト)ではありません";
                        }
                        else
                        {
                            ReadReport elementReport;
                            ReadMembers(value.At(i), *array.element.GetStructType(), array.at(address, i), toHandle, registry, "",
                                        elementReport);
                            if (!elementReport.warnings.empty())
                            {
                                error = elementReport.warnings[0];
                            }
                        }
                    }
                    else
                    {
                        error = ValueToProperty(value.At(i), array.element, array.at(address, i), toHandle, registry);
                    }
                    if (!error.empty() && firstError.empty())
                    {
                        firstError = "[" + std::to_string(i) + "] " + error;
                    }
                }
                return firstError;
            }
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
            case PropertyType::ObjectRef:
            {
                // 番号は ObjectHandle と同じ。読めた相手が、指せる型(ObjectRef<T> の T)でなければ、つながずに null にする。
                core::ObjectHandle handle;
                std::string error = ValueToHandle(value, toHandle, handle);
                core::ObjectReference& reference = *property.GetValuePtrAt<core::ObjectReference>(container);
                if (!error.empty())
                {
                    return error;
                }
                const core::TypeInfo* expected = property.GetObjectRefType();
                const core::Object* target = registry != nullptr && !handle.IsNull() ? registry->Resolve(handle) : nullptr;
                if (target != nullptr && expected != nullptr && !target->GetType().IsA(*expected))
                {
                    reference.handle = {};
                    return std::string("型が違うのでつなぎません(") + target->GetType().GetName() + " は " +
                           expected->GetName() + " ではありません)";
                }
                reference.handle = handle;
                return {};
            }
            case PropertyType::Guid:
            {
                core::Guid guid; // null なら、振られていない(0)
                if (!value.IsNull()
                    && (value.GetType() != ArchiveValue::Type::String || !core::Guid::Parse(value.AsString(), guid)))
                {
                    return "GUID の文字列ではありません";
                }
                *property.GetValuePtrAt<core::Guid>(container) = guid;
                return {};
            }
            }
            return "知らない種類のプロパティです";
        }

        // 「名前 → 値」のオブジェクト(members)を、型 type の持ち主(container)のプロパティに書き込む。
        // 構造体のプロパティは、中に入って同じことを繰り返す。warnings の名前には「color.r」のように道筋を付ける。
        void ReadMembers(const ArchiveValue& members, const core::TypeInfo& type, void* container,
                         const IdToHandle& toHandle, const core::ObjectRegistry* registry, const std::string& prefix,
                         ReadReport& report)
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
                    ReadMembers(value, *property->GetStructType(), property->GetStructAddressAt(container), toHandle, registry,
                                name + ".", report);
                    continue;
                }
                const std::string error = ValueToProperty(value, *property, container, toHandle, registry);
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

        // 読むだけの印(読み込んだアセット)が付いたものには、1つも書かない(SD-2)。
        // エディタの property.set もここを通るので、印を見る場所はここ1か所でよい。
        if (object.IsReadOnly())
        {
            report.warnings.push_back(std::string("読むだけのオブジェクト(読み込んだアセット)には書けません: ") + type.GetName());
            return report;
        }

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

        ReadMembers(*properties, type, &object, toHandle, object.GetOwnerRegistry(), "", report);
        return report;
    }
}
