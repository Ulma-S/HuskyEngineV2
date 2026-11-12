// Prefab.h の実装。読み書きは SceneSerializer に任せ、ここでは「アクターが1つ」「guid を書かない」と、プレハブとの違いの集め方を決める。
#include "engine/Prefab.h"

#include "engine/ObjectSerializer.h"
#include "engine/SceneSerializer.h"

#include <cstdint>
#include <utility>

namespace husky::engine::Prefab
{
    WriteResult Write(Actor& actor)
    {
        WriteResult result;
        Scene* scene = actor.GetScene();
        if (scene == nullptr || actor.IsPendingDestroy())
        {
            result.warnings.push_back("アクターがシーンに入っていないか、Destroy 済みです");
            return result;
        }

        SceneSerializer::WriteOptions options;
        options.writeActorIdentity = false; // 置くたびに新しい guid を振り、元のプレハブは置いたときに書くため
        SceneSerializer::WriteResult saved = SceneSerializer::WriteActors(*scene, {&actor}, options);
        result.value = std::move(saved.value);
        result.warnings = std::move(saved.warnings);
        result.ok = true;
        return result;
    }

    InstantiateResult Instantiate(const ArchiveValue& prefab, Scene& scene, const core::TypeRegistry& types)
    {
        InstantiateResult result;

        // 先に形を確かめる。読み始めてから断ると、作りかけのオブジェクトが残ってしまうため
        // (シーンに入らないので GC で回収はされるが、黙って作らないほうが分かりやすい)。
        const ArchiveValue* actors = prefab.Find("actors");
        if (actors == nullptr || actors->GetType() != ArchiveValue::Type::Array || actors->GetArraySize() != 1)
        {
            result.warnings.push_back("プレハブの \"actors\" は、ちょうど1つのアクターを指してください");
            return result;
        }

        SceneSerializer::ReadReport report = SceneSerializer::Read(prefab, scene, types);
        result.warnings = std::move(report.warnings);
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry != nullptr && report.addedActors.size() == 1)
        {
            result.actor = registry->Resolve<Actor>(report.addedActors[0]);
        }
        return result;
    }

    InstantiateResult Instantiate(const PrefabAsset& prefab, Scene& scene, const core::TypeRegistry& types)
    {
        if (!prefab.IsLoaded())
        {
            InstantiateResult result;
            result.warnings.push_back("プレハブ " + prefab.GetGuid().ToString() + " は読み込み中か、読めませんでした");
            return result;
        }
        InstantiateResult result = Instantiate(prefab.GetData(), scene, types);
        if (result.actor != nullptr)
        {
            result.actor->prefab.guid = prefab.GetGuid();
            result.actor->prefab.asset = prefab.GetHandle(); // 強い参照: 置いたアクターがいる間、アセットも残る
        }
        return result;
    }

    namespace
    {
        // プレハブの値の木の "objects" から、番号 id の記録を探す(プレハブは小さいので、端から探す)。
        const ArchiveValue* FindPrefabEntry(const ArchiveValue& prefab, std::int64_t id)
        {
            const ArchiveValue* objects = prefab.Find("objects");
            for (std::size_t i = 0; objects != nullptr && i < objects->GetArraySize(); ++i)
            {
                const ArchiveValue* entryId = objects->At(i).Find("id");
                if (entryId != nullptr && entryId->GetType() == ArchiveValue::Type::Int && entryId->AsInt() == id)
                {
                    return &objects->At(i);
                }
            }
            return nullptr;
        }

        // 1つのオブジェクトの値(instance)を、プレハブの値(base)と比べ、違う値を out に足す。
        // 構造体は中まで比べ、道筋を点でつなぐ。ハンドルと guid は比べない。プレハブに無い名前も飛ばす。
        void CompareMembers(const ArchiveValue& instance, const ArchiveValue& base, const core::TypeInfo& type,
                            const std::string& component, const std::string& prefix, std::vector<Override>& out)
        {
            for (std::size_t i = 0; i < instance.GetMemberCount(); ++i)
            {
                const std::string& name = instance.GetMemberName(i);
                const ArchiveValue& value = instance.GetMemberValue(i);
                const ArchiveValue* baseValue = base.Find(name);
                const core::Property* property = type.FindProperty(name.c_str());
                if (baseValue == nullptr || property == nullptr || property->GetType() == core::PropertyType::ObjectHandle ||
                    property->GetType() == core::PropertyType::ObjectHandleArray ||
                    property->GetType() == core::PropertyType::Guid)
                {
                    continue;
                }
                const std::string path = prefix + name;
                if (property->GetType() == core::PropertyType::Struct && value.GetType() == ArchiveValue::Type::Object &&
                    baseValue->GetType() == ArchiveValue::Type::Object)
                {
                    CompareMembers(value, *baseValue, *property->GetStructType(), component, path + ".", out);
                    continue;
                }
                if (!value.Equals(*baseValue))
                {
                    Override item;
                    item.component = component;
                    item.path = path;
                    item.value = value;
                    item.prefabValue = *baseValue;
                    item.perInstance = component.empty() && (path == "name" || path.rfind("transform.position", 0) == 0 ||
                                                             path.rfind("transform.rotation", 0) == 0);
                    out.push_back(std::move(item));
                }
            }
        }
    }

    OverridesResult FindOverrides(const Actor& actor)
    {
        OverridesResult result;
        const Scene* scene = actor.GetScene();
        const core::ObjectRegistry* registry = scene != nullptr ? scene->GetObjectRegistry() : nullptr;
        if (actor.prefab.IsNull())
        {
            result.error = "the actor was not placed from a prefab";
            return result;
        }
        const PrefabAsset* prefab = registry != nullptr ? registry->Resolve<PrefabAsset>(actor.prefab.asset) : nullptr;
        if (prefab == nullptr || !prefab->IsLoaded() || prefab->GetGuid() != actor.prefab.guid)
        {
            result.error = "the prefab " + actor.prefab.guid.ToString() + " is not loaded";
            return result;
        }

        // プレハブのアクターと、そのコンポーネントの記録(並んだ順)
        const ArchiveValue& data = prefab->GetData();
        const ArchiveValue* actors = data.Find("actors");
        const ArchiveValue* actorEntry =
            actors != nullptr && actors->GetArraySize() == 1 ? FindPrefabEntry(data, actors->At(0).AsInt(-1)) : nullptr;
        const ArchiveValue* actorProperties = actorEntry != nullptr ? actorEntry->Find("properties") : nullptr;
        if (actorProperties == nullptr)
        {
            result.error = "the prefab " + prefab->GetGuid().ToString() + " has no actor";
            return result;
        }
        std::vector<const ArchiveValue*> prefabComponents;
        const ArchiveValue* componentIds = actorProperties->Find("components");
        for (std::size_t i = 0; componentIds != nullptr && i < componentIds->GetArraySize(); ++i)
        {
            prefabComponents.push_back(FindPrefabEntry(data, componentIds->At(i).AsInt(-1)));
        }

        // 置いたものの、生きているコンポーネント。数と型(並んだ順)がプレハブと同じでなければ比べられない
        std::vector<const Component*> components;
        for (const core::ObjectHandle handle : actor.components)
        {
            const Component* component = registry->Resolve<Component>(handle);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                components.push_back(component);
            }
        }
        bool sameShape = components.size() == prefabComponents.size();
        for (std::size_t i = 0; sameShape && i < components.size(); ++i)
        {
            const ArchiveValue* type = prefabComponents[i] != nullptr ? prefabComponents[i]->Find("type") : nullptr;
            sameShape = type != nullptr && prefabComponents[i]->Find("properties") != nullptr &&
                        type->AsString() == components[i]->GetType().GetName();
        }
        if (!sameShape)
        {
            result.error = "the components differ from the prefab (added or removed components are not supported yet)";
            return result;
        }

        result.prefab = prefab;
        CompareMembers(*ObjectSerializer::Write(actor).Find("properties"), *actorProperties, actor.GetType(), std::string(),
                       std::string(), result.overrides);
        for (std::size_t i = 0; i < components.size(); ++i)
        {
            CompareMembers(*ObjectSerializer::Write(*components[i]).Find("properties"),
                           *prefabComponents[i]->Find("properties"), components[i]->GetType(),
                           components[i]->GetType().GetName(), std::string(), result.overrides);
        }
        return result;
    }
}
