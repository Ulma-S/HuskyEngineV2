// SceneSerializer.h の実装。
#include "engine/SceneSerializer.h"

#include "engine/ObjectSerializer.h"

#include <algorithm> // std::find
#include <cstdint>
#include <unordered_map> // std::unordered_map(C# の Dictionary にあたる、ハッシュ表)
#include <unordered_set> // std::unordered_set(C# の HashSet にあたる)
#include <string_view>
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
        void CollectObjects(Scene& scene, core::ObjectRegistry& registry, const std::vector<Actor*>& actors,
                            std::vector<const core::Object*>& outObjects, std::vector<std::int64_t>& outActorIds)
        {
            for (Actor* actor : actors)
            {
                if (actor == nullptr || actor->IsPendingDestroy() || actor->GetScene() != &scene)
                {
                    continue;
                }
                outActorIds.push_back(static_cast<std::int64_t>(outObjects.size()));
                outObjects.push_back(actor);
                for (const core::ObjectHandle handle : actor->components)
                {
                    const Component* component = registry.Resolve<Component>(handle);
                    if (component != nullptr && !component->IsPendingDestroy())
                    {
                        outObjects.push_back(component);
                    }
                }
            }
        }

        // オブジェクトの記録("type" の名前)。無ければ空。
        std::string TypeNameOf(const ArchiveValue& entry)
        {
            const ArchiveValue* type = entry.Find("type");
            return type != nullptr ? type->AsString() : std::string();
        }

        // 「番号 → オブジェクトの記録」の表。同じ番号が2回あれば、先のほう(2回目は CreateObjects が警告する)。
        using EntryIndex = std::unordered_map<std::int64_t, const ArchiveValue*>;

        EntryIndex IndexEntries(const ArchiveValue& value)
        {
            EntryIndex index;
            const ArchiveValue* objectArray = value.Find("objects");
            for (std::size_t i = 0; objectArray != nullptr && i < objectArray->GetArraySize(); ++i)
            {
                const ArchiveValue* id = objectArray->At(i).Find("id");
                if (id != nullptr && id->GetType() == ArchiveValue::Type::Int)
                {
                    index.emplace(id->AsInt(), &objectArray->At(i)); // emplace は、すでにある番号なら何もしない
                }
            }
            return index;
        }

        const ArchiveValue* FindEntry(const EntryIndex& index, std::int64_t id)
        {
            const auto found = index.find(id);
            return found != index.end() ? found->second : nullptr;
        }

        // 1体のアクターの記録と、そのコンポーネントの記録(並んだ順)。
        struct ActorEntries
        {
            const ArchiveValue* actor = nullptr;
            std::vector<const ArchiveValue*> components;
            std::vector<std::int64_t> componentIds; // コンポーネントの番号(components と同じ順)
        };

        // 番号 actorId のアクターの記録と、その "components" が指す記録を集める。形がおかしければ false。
        bool GetActorEntries(const EntryIndex& index, std::int64_t actorId, ActorEntries& out)
        {
            out.actor = FindEntry(index, actorId);
            const ArchiveValue* properties = out.actor != nullptr ? out.actor->Find("properties") : nullptr;
            if (properties == nullptr)
            {
                return false;
            }
            const ArchiveValue* components = properties->Find("components");
            for (std::size_t i = 0; components != nullptr && i < components->GetArraySize(); ++i)
            {
                const std::int64_t id = components->At(i).AsInt(-1);
                const ArchiveValue* component = FindEntry(index, id);
                if (component == nullptr || component->Find("properties") == nullptr)
                {
                    return false;
                }
                out.components.push_back(component);
                out.componentIds.push_back(id);
            }
            return true;
        }

        // プレハブの値の木の中の、ただ1体のアクターの記録を集める。
        bool GetPrefabEntries(const ArchiveValue& prefab, ActorEntries& out)
        {
            const ArchiveValue* actors = prefab.Find("actors");
            return actors != nullptr && actors->GetArraySize() == 1 &&
                   GetActorEntries(IndexEntries(prefab), actors->At(0).AsInt(-1), out);
        }

        // instance(置いたものの値)のうち、base(プレハブの値)と違うものだけを残した値を返す。type は持ち主の型。
        //   - 構造体は中まで比べ、違うメンバーだけを残す(position.x だけが違えば、{"position": {"x": ...}})
        //   - ハンドルは、ファイルの中の番号の振り方がシーンとプレハブで違い、比べられないので、いつも残す
        //   - alwaysKeep の名前(アクターの guid など)は、同じでも残す
        ArchiveValue DiffMembers(const ArchiveValue& instance, const ArchiveValue& base, const core::TypeInfo& type,
                                 const std::vector<std::string_view>& alwaysKeep = {})
        {
            ArchiveValue diff = ArchiveValue::MakeObject();
            for (std::size_t i = 0; i < instance.GetMemberCount(); ++i)
            {
                const std::string& name = instance.GetMemberName(i);
                const ArchiveValue& value = instance.GetMemberValue(i);
                const ArchiveValue* baseValue = base.Find(name);
                const core::Property* property = type.FindProperty(name.c_str());
                const bool keep = std::find(alwaysKeep.begin(), alwaysKeep.end(), name) != alwaysKeep.end();
                if (!keep && property != nullptr && property->GetType() == core::PropertyType::Struct &&
                    baseValue != nullptr && baseValue->GetType() == ArchiveValue::Type::Object &&
                    value.GetType() == ArchiveValue::Type::Object)
                {
                    ArchiveValue inner = DiffMembers(value, *baseValue, *property->GetStructType());
                    if (inner.GetMemberCount() > 0)
                    {
                        diff.Set(name, std::move(inner));
                    }
                    continue;
                }
                const bool isHandle = property != nullptr && (property->GetType() == core::PropertyType::ObjectHandle ||
                                                              property->GetType() == core::PropertyType::ObjectHandleArray);
                if (keep || isHandle || baseValue == nullptr || !value.Equals(*baseValue))
                {
                    diff.Set(name, value);
                }
            }
            return diff;
        }

        // 2体のアクターの記録が、同じ形(アクターの型、コンポーネントの数と、並んだ順の型)か。
        bool SameShape(const ActorEntries& a, const ActorEntries& b)
        {
            if (TypeNameOf(*a.actor) != TypeNameOf(*b.actor) || a.components.size() != b.components.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.components.size(); ++i)
            {
                if (TypeNameOf(*a.components[i]) != TypeNameOf(*b.components[i]))
                {
                    return false;
                }
            }
            return true;
        }

        // 読み込みの1回目: "objects" の全記録を、型の名前から作る。「ファイルの番号 → 新しいハンドル」を handleOf に足し、
        // (記録、作ったオブジェクト)の組を created に足す。skip にある番号は、すでに作ってあるので飛ばす(1回ずつ)。
        void CreateObjects(const ArchiveValue& objectArray, core::ObjectRegistry& registry, const core::TypeRegistry& types,
                           std::unordered_map<std::int64_t, core::ObjectHandle>& handleOf,
                           std::vector<std::pair<const ArchiveValue*, core::Object*>>& created,
                           std::unordered_set<std::int64_t>& skip, ReadReport& report)
        {
            for (std::size_t i = 0; i < objectArray.GetArraySize(); ++i)
            {
                const ArchiveValue& entry = objectArray.At(i);
                const ArchiveValue* id = entry.Find("id");
                const ArchiveValue* type = entry.Find("type");
                if (id == nullptr || id->GetType() != ArchiveValue::Type::Int || type == nullptr)
                {
                    report.warnings.push_back(std::to_string(i) + " 番目: \"id\" か \"type\" がありません");
                    continue;
                }
                if (skip.erase(id->AsInt()) > 0) // プレハブから作ったもの(erase は、取り除いた数を返す)
                {
                    continue;
                }
                if (handleOf.count(id->AsInt()) != 0)
                {
                    report.warnings.push_back("番号 " + std::to_string(id->AsInt()) + " が2回あります");
                    continue;
                }
                core::Object* object = types.Create(type->AsString(), registry);
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
        }

        // 読み込みの2回目: 作ったオブジェクトに、記録のプロパティを読む。ハンドルは handleOf で番号を新しいハンドルに付け替える。
        void ReadObjects(const std::vector<std::pair<const ArchiveValue*, core::Object*>>& created,
                         const std::unordered_map<std::int64_t, core::ObjectHandle>& handleOf, const std::string& prefix,
                         ReadReport& report)
        {
            const ObjectSerializer::IdToHandle toHandle = [&handleOf](std::int64_t id) -> core::ObjectHandle {
                const auto found = handleOf.find(id);
                return found != handleOf.end() ? found->second : core::ObjectHandle{};
            };
            for (const auto& [entry, object] : created) // 構造化束縛: pair の first と second に、名前を付けて取り出す
            {
                ObjectSerializer::ReadReport objectReport = ObjectSerializer::ReadProperties(*entry, *object, toHandle);
                for (std::string& warning : objectReport.warnings)
                {
                    report.warnings.push_back(prefix + "番号 " + std::to_string(entry->Find("id")->AsInt()) + ": " + warning);
                }
            }
        }

        // プレハブの値の木から、アクター1体とそのコンポーネントを作って値を読む(シーンにはまだ入れない)。作れなければ nullptr。
        Actor* CreateFromPrefab(const PrefabAsset& prefab, core::ObjectRegistry& registry, const core::TypeRegistry& types,
                                ReadReport& report)
        {
            const ArchiveValue* objectArray = prefab.GetData().Find("objects");
            const ArchiveValue* actors = prefab.GetData().Find("actors");
            if (objectArray == nullptr || actors == nullptr || actors->GetArraySize() != 1)
            {
                return nullptr;
            }
            std::unordered_map<std::int64_t, core::ObjectHandle> handleOf;
            std::vector<std::pair<const ArchiveValue*, core::Object*>> created;
            std::unordered_set<std::int64_t> noSkip;
            ReadReport prefabReport;
            CreateObjects(*objectArray, registry, types, handleOf, created, noSkip, prefabReport);
            ReadObjects(created, handleOf, "", prefabReport);
            const std::string prefix = "プレハブ " + prefab.GetGuid().ToString() + ": ";
            for (const std::string& warning : prefabReport.warnings)
            {
                report.warnings.push_back(prefix + warning);
            }
            const auto found = handleOf.find(actors->At(0).AsInt(-1));
            return found != handleOf.end() ? registry.Resolve<Actor>(found->second) : nullptr;
        }
    }

    WriteResult Write(Scene& scene)
    {
        std::vector<Actor*> actors;
        scene.ForEachActor([&actors](Actor& actor) { actors.push_back(&actor); }); // Destroy 済みのアクターは渡されない
        return WriteActors(scene, actors);
    }

    WriteResult WriteActors(Scene& scene, const std::vector<Actor*>& actors, const WriteOptions& options)
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
        CollectObjects(scene, *registry, actors, objects, actorIds);

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

        // 3. プレハブから置いたアクターは、比べる相手(プレハブの中の、同じ位置のオブジェクトの値)を決める。
        //    baseOf[番号] が nullptr でなければ、そのオブジェクトはプレハブとの違いだけを書く。
        //    プレハブそのものを書くとき(writeActorIdentity が false)は、違いではなく全部を書く。
        std::vector<const ArchiveValue*> baseOf(objects.size(), nullptr);
        for (std::size_t k = 0; options.writeActorIdentity && k < actorIds.size(); ++k)
        {
            const std::size_t actorIndex = static_cast<std::size_t>(actorIds[k]);
            const std::size_t end = k + 1 < actorIds.size() ? static_cast<std::size_t>(actorIds[k + 1]) : objects.size();
            // アクターの番号の一覧から来たので、中身は Actor(単一継承なので static_cast で戻せる。RTTI は使わない)
            const Actor& actor = static_cast<const Actor&>(*objects[actorIndex]);
            if (actor.prefab.IsNull())
            {
                continue;
            }
            const PrefabAsset* prefab = registry->Resolve<PrefabAsset>(actor.prefab.asset);
            ActorEntries base;
            if (prefab == nullptr || !prefab->IsLoaded() || prefab->GetGuid() != actor.prefab.guid ||
                !GetPrefabEntries(prefab->GetData(), base))
            {
                result.warnings.push_back("アクター " + actor.name + ": プレハブ " + actor.prefab.guid.ToString() +
                                          " が読み込まれていないので、違いではなく全部の値を書きます");
                continue;
            }
            bool sameShape = TypeNameOf(*base.actor) == actor.GetType().GetName() &&
                             base.components.size() == end - actorIndex - 1;
            for (std::size_t c = 0; sameShape && c < base.components.size(); ++c)
            {
                sameShape = TypeNameOf(*base.components[c]) == objects[actorIndex + 1 + c]->GetType().GetName();
            }
            if (!sameShape)
            {
                result.warnings.push_back("アクター " + actor.name + ": コンポーネントの並びがプレハブと違うので、"
                                          "違いではなく全部の値を書きます(足した・外したコンポーネントは、まだ違いとして書けません)");
                continue;
            }
            baseOf[actorIndex] = base.actor->Find("properties");
            for (std::size_t c = 0; c < base.components.size(); ++c)
            {
                baseOf[actorIndex + 1 + c] = base.components[c]->Find("properties");
            }
        }

        // 4. オブジェクトを1つずつ書く。読みやすいように、"id" を先頭に置く。
        result.value = ArchiveValue::MakeObject();
        result.value.Set("version", ArchiveValue(FormatVersion));
        ArchiveValue& objectArray = result.value.Set("objects", ArchiveValue::MakeArray());
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            ArchiveValue written = ObjectSerializer::Write(*objects[i], toId);
            ArchiveValue& entry = objectArray.Append(ArchiveValue::MakeObject());
            entry.Set("id", ArchiveValue(static_cast<std::int64_t>(i)));
            entry.Set("type", *written.Find("type"));
            ArchiveValue& properties = entry.Set("properties", *written.Find("properties"));
            // アクターかどうかは、actorIds(アクターの番号の一覧)で分かる(RTTI を使わないので dynamic_cast はしない)
            const bool isActor =
                std::find(actorIds.begin(), actorIds.end(), static_cast<std::int64_t>(i)) != actorIds.end();
            if (baseOf[i] != nullptr)
            {
                // アクターの guid、prefab、components は、同じでも書く(この1体であること、元のプレハブ、番号の対応に要る)
                static const std::vector<std::string_view> actorKeep = {"guid", "prefab", "components"};
                properties = DiffMembers(properties, *baseOf[i], objects[i]->GetType(),
                                         isActor ? actorKeep : std::vector<std::string_view>{});
            }
            if (!options.writeActorIdentity && isActor)
            {
                properties.Remove("guid");
                properties.Remove("prefab");
            }
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

    ReadReport Read(const ArchiveValue& value, Scene& scene, const core::TypeRegistry& types,
                    const PrefabFinder& findPrefab)
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
        const ArchiveValue* actorArray = value.Find("actors");

        std::unordered_map<std::int64_t, core::ObjectHandle> handleOf; // ファイルの番号 → 新しいハンドル
        std::vector<std::pair<const ArchiveValue*, core::Object*>> created; // 2回目で読む組(ファイルの値、作ったオブジェクト)

        // 0回目: プレハブから置いたアクターを、プレハブから作る(プレハブの値が入る)。
        //        2回目で、その上にファイルの値(プレハブとの違い)を重ねる。ファイルの番号とは、並んだ順で組にする。
        std::unordered_set<std::int64_t> fromPrefab;                          // プレハブから作った番号(1回目で作らない)
        std::vector<std::pair<core::ObjectHandle, core::ObjectHandle>> linked; // (アクター、プレハブのアセット)
        const EntryIndex index = findPrefab ? IndexEntries(value) : EntryIndex{};
        for (std::size_t i = 0; findPrefab && actorArray != nullptr && i < actorArray->GetArraySize(); ++i)
        {
            const std::int64_t id = actorArray->At(i).AsInt(-1);
            ActorEntries file;
            if (!GetActorEntries(index, id, file))
            {
                continue; // 形がおかしいものは、1回目と 3. で警告が出る
            }
            const ArchiveValue* prefabValue = file.actor->Find("properties")->Find("prefab");
            core::Guid guid;
            if (prefabValue == nullptr || prefabValue->GetType() != ArchiveValue::Type::String ||
                !core::Guid::Parse(prefabValue->AsString(), guid) || guid.IsNull())
            {
                continue; // プレハブから置いたものではない
            }
            const std::string where = "番号 " + std::to_string(id) + ": プレハブ " + guid.ToString();
            const PrefabAsset* prefab = findPrefab(guid);
            ActorEntries base;
            if (prefab == nullptr || !prefab->IsLoaded() || !GetPrefabEntries(prefab->GetData(), base))
            {
                report.warnings.push_back(where + " を読めないので、ファイルにある値だけで作ります(書いていない値は型の既定値になります)");
                continue;
            }
            if (!SameShape(file, base))
            {
                report.warnings.push_back(where + " とコンポーネントの並びが違うので、ファイルにある値だけで作ります");
                continue;
            }
            Actor* actor = CreateFromPrefab(*prefab, *registry, types, report);
            if (actor == nullptr || actor->components.size() != file.components.size())
            {
                // 作りかけのものはシーンに入らないので、GC で回収される
                report.warnings.push_back(where + " から作れないので、ファイルにある値だけで作ります");
                continue;
            }
            handleOf[id] = actor->GetHandle();
            created.emplace_back(file.actor, actor);
            fromPrefab.insert(id);
            for (std::size_t c = 0; c < file.components.size(); ++c)
            {
                handleOf[file.componentIds[c]] = actor->components[c];
                created.emplace_back(file.components[c], registry->Resolve<Component>(actor->components[c]));
                fromPrefab.insert(file.componentIds[c]);
            }
            report.createdCount += 1 + file.components.size();
            linked.emplace_back(actor->GetHandle(), prefab->GetHandle());
        }

        // 1回目: 残りの全オブジェクトを、型の名前から作る。
        CreateObjects(*objectArray, *registry, types, handleOf, created, fromPrefab, report);
        // 2回目: プロパティを読む。ハンドルは、表で番号を新しいハンドルに付け替える。
        ReadObjects(created, handleOf, "", report);

        // プレハブから作ったアクターに、読み込んだプレハブを覚えさせる(ファイルからは GUID しか読まないため)。
        // 次に保存するときに、このプレハブと比べて違いだけを書ける。
        for (const auto& [actorHandle, prefabHandle] : linked)
        {
            Actor* actor = registry->Resolve<Actor>(actorHandle);
            const PrefabAsset* prefab = registry->Resolve<PrefabAsset>(prefabHandle);
            if (actor != nullptr && prefab != nullptr && actor->prefab.guid == prefab->GetGuid())
            {
                actor->prefab.asset = prefabHandle;
            }
        }

        // 3. アクターを、ファイルに書いてある順でシーンに入れる(コンポーネントの持ち主も、ここで付け直される)。
        //    変わらない ID(Actor::guid)が、シーンにすでにいる(生きている)アクターや、このファイルの前のアクターと重なっていたら、
        //    新しい ID を振り直して警告を出す(ファイルを手でコピーした、同じファイルを2回読んだ、など)。
        //    黙ってどちらかを使うと、コマンドが別のアクターを書き換えてしまうため(アセットの GUID の重なりと同じ考え方)。
        std::unordered_set<core::Guid> usedGuids;
        scene.ForEachActor([&usedGuids](Actor& existing) { usedGuids.insert(existing.guid); }); // Destroy 済みは入らない
        for (std::size_t i = 0; actorArray != nullptr && i < actorArray->GetArraySize(); ++i)
        {
            const std::int64_t id = actorArray->At(i).AsInt(-1);
            const auto found = handleOf.find(id);
            Actor* actor = found != handleOf.end() ? registry->Resolve<Actor>(found->second) : nullptr;
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
            report.addedActors.push_back(actor->GetHandle());
        }
        return report;
    }
}
