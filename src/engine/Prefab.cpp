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

    namespace
    {
        // "color.r" のような道筋を、点で区切る。
        std::vector<std::string> SplitPath(const std::string& path)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (start <= path.size())
            {
                const std::size_t dot = path.find('.', start);
                const std::size_t end = dot == std::string::npos ? path.size() : dot;
                parts.push_back(path.substr(start, end - start));
                start = end + 1;
            }
            return parts;
        }

        // tree の、道筋 parts の位置に value を入れた写しを返す(途中のオブジェクトが無ければ作る)。
        // ArchiveValue は中のメンバーを書き換える口を持たないので、外側から順に作り直す。
        ArchiveValue WithValueAt(const ArchiveValue& tree, const std::vector<std::string>& parts, std::size_t index,
                                 const ArchiveValue& value)
        {
            ArchiveValue copy = tree.GetType() == ArchiveValue::Type::Object ? tree : ArchiveValue::MakeObject();
            if (index + 1 == parts.size())
            {
                copy.Set(parts[index], value);
                return copy;
            }
            const ArchiveValue* child = tree.Find(parts[index]);
            copy.Set(parts[index], WithValueAt(child != nullptr ? *child : ArchiveValue::MakeObject(), parts, index + 1, value));
            return copy;
        }

        // 値1つを、ファイルと同じ入れ子の形({"properties": {"color": {"r": 値}}})にする(ReadProperties に渡す)。
        ArchiveValue MakePropertyEntry(const std::string& path, const ArchiveValue& value)
        {
            ArchiveValue entry = ArchiveValue::MakeObject();
            entry.Set("properties", WithValueAt(ArchiveValue::MakeObject(), SplitPath(path), 0, value));
            return entry;
        }
    }

    bool WriteChanges(const ArchiveValue& data, const std::vector<ValueChange>& changes, ArchiveValue& out,
                      std::string& error)
    {
        const ArchiveValue* objects = data.Find("objects");
        const ArchiveValue* actors = data.Find("actors");
        if (objects == nullptr || actors == nullptr || actors->GetArraySize() != 1)
        {
            error = "the prefab has no actor";
            return false;
        }
        std::vector<ArchiveValue> entries; // 書き換える写し("objects" と同じ順)
        for (std::size_t i = 0; i < objects->GetArraySize(); ++i)
        {
            entries.push_back(objects->At(i));
        }
        // 番号 id の記録の位置(entries の中)。無ければ entries.size()
        auto indexOf = [&entries](std::int64_t id) {
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const ArchiveValue* entryId = entries[i].Find("id");
                if (entryId != nullptr && entryId->AsInt(-1) == id)
                {
                    return i;
                }
            }
            return entries.size();
        };

        const std::size_t actorIndex = indexOf(actors->At(0).AsInt(-1));
        const ArchiveValue* actorProperties = actorIndex < entries.size() ? entries[actorIndex].Find("properties") : nullptr;
        if (actorProperties == nullptr)
        {
            error = "the prefab has no actor";
            return false;
        }
        const ArchiveValue componentIds = actorProperties->Find("components") != nullptr ? *actorProperties->Find("components")
                                                                                         : ArchiveValue::MakeArray();
        for (const ValueChange& change : changes)
        {
            // 書き込む記録: アクター自身か、型の名前が合う最初のコンポーネント
            std::size_t target = actorIndex;
            if (!change.component.empty())
            {
                target = entries.size();
                for (std::size_t i = 0; i < componentIds.GetArraySize() && target == entries.size(); ++i)
                {
                    const std::size_t index = indexOf(componentIds.At(i).AsInt(-1));
                    const ArchiveValue* type = index < entries.size() ? entries[index].Find("type") : nullptr;
                    if (type != nullptr && type->AsString() == change.component)
                    {
                        target = index;
                    }
                }
                if (target == entries.size())
                {
                    error = "the prefab has no component " + change.component;
                    return false;
                }
            }
            const ArchiveValue* properties = entries[target].Find("properties");
            entries[target].Set("properties", WithValueAt(properties != nullptr ? *properties : ArchiveValue::MakeObject(),
                                                          SplitPath(change.path), 0, change.newValue));
        }

        out = data;
        ArchiveValue& newObjects = out.Set("objects", ArchiveValue::MakeArray());
        for (ArchiveValue& entry : entries)
        {
            newObjects.Append(std::move(entry));
        }
        return true;
    }

    std::size_t FollowChanges(Actor& actor, const std::vector<ValueChange>& changes)
    {
        const Scene* scene = actor.GetScene();
        const core::ObjectRegistry* registry = scene != nullptr ? scene->GetObjectRegistry() : nullptr;
        std::size_t changed = 0;
        for (const ValueChange& change : changes)
        {
            // 書き換える相手: アクター自身か、型の名前が合う最初の(生きている)コンポーネント
            core::Object* owner = change.component.empty() ? static_cast<core::Object*>(&actor) : nullptr;
            for (std::size_t i = 0; owner == nullptr && registry != nullptr && i < actor.components.size(); ++i)
            {
                Component* component = registry->Resolve<Component>(actor.components[i]);
                if (component != nullptr && !component->IsPendingDestroy() && component->GetType().GetName() == change.component)
                {
                    owner = component;
                }
            }
            if (owner == nullptr)
            {
                continue;
            }
            // 今の値を、保存と同じ道具で値の木にして、道筋のとおりに取り出す
            const ArchiveValue current = ObjectSerializer::Write(*owner);
            const ArchiveValue* value = current.Find("properties");
            for (const std::string& part : SplitPath(change.path))
            {
                value = value != nullptr ? value->Find(part) : nullptr;
            }
            if (value != nullptr && value->Equals(change.oldValue)) // プレハブの古い値のまま = 違いではない
            {
                ObjectSerializer::ReadProperties(MakePropertyEntry(change.path, change.newValue), *owner);
                ++changed;
            }
        }
        return changed;
    }
}
