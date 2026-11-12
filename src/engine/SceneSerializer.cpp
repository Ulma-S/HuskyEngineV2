// SceneSerializer.h の実装。
#include "engine/SceneSerializer.h"

#include "engine/ObjectSerializer.h"

#include <cstdint>
#include <unordered_map> // std::unordered_map(C# の Dictionary にあたる、ハッシュ表)
#include <unordered_set> // std::unordered_set(C# の HashSet にあたる)
#include <utility>

namespace husky::engine::SceneSerializer
{
    namespace
    {
        // ハンドルを、ハッシュ表の鍵にする 64 ビットの数にする(上位 32 ビットに場所、下位 32 ビットに世代)。
        std::uint64_t ToKey(core::ObjectHandle handle)
        {
            return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
        }

        // 保存するオブジェクトを、番号の順に集める: アクター、その後ろにそのアクターのコンポーネント、の繰り返し。
        void CollectObjects(Scene& scene, core::ObjectRegistry& registry, std::vector<const core::Object*>& outObjects,
                            std::vector<std::int64_t>& outActorIds)
        {
            scene.ForEachActor([&](Actor& actor) { // Destroy 済みのアクターは渡されない
                outActorIds.push_back(static_cast<std::int64_t>(outObjects.size()));
                outObjects.push_back(&actor);
                for (const core::ObjectHandle handle : actor.components)
                {
                    const Component* component = registry.Resolve<Component>(handle);
                    if (component != nullptr && !component->IsPendingDestroy())
                    {
                        outObjects.push_back(component);
                    }
                }
            });
        }
    }

    WriteResult Write(Scene& scene)
    {
        WriteResult result;
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            result.warnings.push_back("シーンがレジストリで作られていません");
            return result;
        }

        // 1. 保存するオブジェクトを集めて、番号(並んだ順の位置)を振る。
        std::vector<const core::Object*> objects;
        std::vector<std::int64_t> actorIds;
        CollectObjects(scene, *registry, objects, actorIds);

        std::unordered_map<std::uint64_t, std::int64_t> idOf; // ハンドル → 番号
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            idOf[ToKey(objects[i]->GetHandle())] = static_cast<std::int64_t>(i);
        }

        // 2. ハンドルを番号に替える関数。保存しない相手なら -1(ObjectSerializer が null と書く)。
        std::size_t missingCount = 0;
        const ObjectSerializer::HandleToId toId = [&](core::ObjectHandle handle) -> std::int64_t {
            const auto found = idOf.find(ToKey(handle));
            if (found == idOf.end())
            {
                ++missingCount;
                return -1;
            }
            return found->second;
        };

        // 3. オブジェクトを1つずつ書く。読みやすいように、"id" を先頭に置く。
        result.value = ArchiveValue::MakeObject();
        result.value.Set("version", ArchiveValue(FormatVersion));
        ArchiveValue& objectArray = result.value.Set("objects", ArchiveValue::MakeArray());
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            ArchiveValue written = ObjectSerializer::Write(*objects[i], toId);
            ArchiveValue& entry = objectArray.Append(ArchiveValue::MakeObject());
            entry.Set("id", ArchiveValue(static_cast<std::int64_t>(i)));
            entry.Set("type", *written.Find("type"));
            entry.Set("properties", *written.Find("properties"));
        }

        ArchiveValue& actorArray = result.value.Set("actors", ArchiveValue::MakeArray());
        for (const std::int64_t id : actorIds)
        {
            actorArray.Append(ArchiveValue(id));
        }

        if (missingCount > 0)
        {
            result.warnings.push_back("保存しないオブジェクトを指すハンドルを null にしました(" +
                                      std::to_string(missingCount) + " 個)");
        }
        return result;
    }

    ReadReport Read(const ArchiveValue& value, Scene& scene, const core::TypeRegistry& types)
    {
        ReadReport report;
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            report.warnings.push_back("シーンがレジストリで作られていません");
            return report;
        }

        const ArchiveValue* version = value.Find("version");
        if (version == nullptr || version->AsInt(-1) != FormatVersion)
        {
            report.warnings.push_back("ファイルの版が違うか、ありません(読めるところまで読みます)");
        }

        const ArchiveValue* objectArray = value.Find("objects");
        if (objectArray == nullptr || objectArray->GetType() != ArchiveValue::Type::Array)
        {
            report.warnings.push_back("\"objects\" がありません");
            return report;
        }

        // 1回目: 全オブジェクトを、型の名前から作る。ファイルの番号 → 新しいハンドル、の表を作る。
        std::unordered_map<std::int64_t, core::ObjectHandle> handleOf;
        std::vector<std::pair<const ArchiveValue*, core::Object*>> created; // 2回目で読む組(ファイルの値、作ったオブジェクト)
        for (std::size_t i = 0; i < objectArray->GetArraySize(); ++i)
        {
            const ArchiveValue& entry = objectArray->At(i);
            const ArchiveValue* id = entry.Find("id");
            const ArchiveValue* type = entry.Find("type");
            if (id == nullptr || id->GetType() != ArchiveValue::Type::Int || type == nullptr)
            {
                report.warnings.push_back(std::to_string(i) + " 番目: \"id\" か \"type\" がありません");
                continue;
            }
            if (handleOf.count(id->AsInt()) != 0)
            {
                report.warnings.push_back("番号 " + std::to_string(id->AsInt()) + " が2回あります");
                continue;
            }
            core::Object* object = types.Create(type->AsString(), *registry);
            if (object == nullptr)
            {
                report.warnings.push_back("番号 " + std::to_string(id->AsInt()) + ": 型 " + type->AsString() +
                                          " を作れません(登録されていないか、メモリが足りません)");
                continue;
            }
            handleOf[id->AsInt()] = object->GetHandle();
            created.emplace_back(&entry, object);
            ++report.createdCount;
        }

        // 2回目: プロパティを読む。ハンドルは、表で番号を新しいハンドルに付け替える。
        const ObjectSerializer::IdToHandle toHandle = [&](std::int64_t id) -> core::ObjectHandle {
            const auto found = handleOf.find(id);
            return found != handleOf.end() ? found->second : core::ObjectHandle{};
        };
        for (const auto& [entry, object] : created) // 構造化束縛: pair の first と second に、名前を付けて取り出す
        {
            ObjectSerializer::ReadReport objectReport = ObjectSerializer::ReadProperties(*entry, *object, toHandle);
            for (std::string& warning : objectReport.warnings)
            {
                report.warnings.push_back("番号 " + std::to_string(entry->Find("id")->AsInt()) + ": " + warning);
            }
        }

        // 3. アクターを、ファイルに書いてある順でシーンに入れる(コンポーネントの持ち主も、ここで付け直される)。
        //    変わらない ID(Actor::guid)が、シーンにすでにいる(生きている)アクターや、このファイルの前のアクターと重なっていたら、
        //    新しい ID を振り直して警告を出す(ファイルを手でコピーした、同じファイルを2回読んだ、など)。
        //    黙ってどちらかを使うと、コマンドが別のアクターを書き換えてしまうため(アセットの GUID の重なりと同じ考え方)。
        std::unordered_set<core::Guid> usedGuids;
        scene.ForEachActor([&usedGuids](Actor& existing) { usedGuids.insert(existing.guid); }); // Destroy 済みは入らない
        const ArchiveValue* actorArray = value.Find("actors");
        for (std::size_t i = 0; actorArray != nullptr && i < actorArray->GetArraySize(); ++i)
        {
            const std::int64_t id = actorArray->At(i).AsInt(-1);
            Actor* actor = registry->Resolve<Actor>(toHandle(id));
            if (actor == nullptr || !scene.AddLoadedActor(*actor)) // guid が無ければ、ここで振られる
            {
                report.warnings.push_back("番号 " + std::to_string(id) + " をシーンに入れられません(アクターではないか、ありません)");
                continue;
            }
            if (!usedGuids.insert(actor->guid).second) // insert の .second は「新しく入ったか」。false なら重なっている
            {
                const std::string old = actor->guid.ToString();
                do
                {
                    actor->guid = core::Guid::Generate();
                } while (!usedGuids.insert(actor->guid).second);
                report.warnings.push_back("番号 " + std::to_string(id) + ": guid " + old +
                                          " がほかのアクターと重なっていたので、新しい guid を振りました");
            }
            ++report.addedActorCount;
        }
        return report;
    }
}
