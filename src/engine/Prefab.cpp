// Prefab.h の実装。中身は SceneSerializer に任せ、ここでは「アクターが1つ」と「guid を書かない」を決めるだけ。
#include "engine/Prefab.h"

#include "engine/SceneSerializer.h"

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
}
