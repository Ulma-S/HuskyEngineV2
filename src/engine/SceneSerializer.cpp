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
                                                              property->GetType() == core::PropertyType::ObjectRef ||
                                                              property->GetType() == core::PropertyType::ObjectHandleArray ||
                                                              property->IsReference()); // 参照の配列(SF-2)も
                if (keep || isHandle || baseValue == nullptr || !value.Equals(*baseValue))
                {
                    diff.Set(name, value);
                }
            }
            return diff;
        }

        // コンポーネントの記録の "prefabComponentId"(Component::prefabComponentId)を読む。無い・読めなければ空の Guid。
        core::Guid ComponentIdOf(const ArchiveValue& entry)
        {
            const ArchiveValue* properties = entry.Find("properties");
            const ArchiveValue* id = properties != nullptr ? properties->Find("prefabComponentId") : nullptr;
            core::Guid guid;
            if (id == nullptr || id->GetType() != ArchiveValue::Type::String || !core::Guid::Parse(id->AsString(), guid))
            {
                return core::Guid{};
            }
            return guid;
        }

        // プレハブのコンポーネントの「ID → base.components の位置」の表を作る。
        // 全部に、重ならない ID があれば true(ID で組にできる)。ID の無い古いプレハブなら false(並び順で組にする)。
        bool IndexPrefabComponents(const ActorEntries& base, std::unordered_map<core::Guid, std::size_t>& out)
        {
            for (std::size_t i = 0; i < base.components.size(); ++i)
            {
                const core::Guid id = ComponentIdOf(*base.components[i]);
                if (id.IsNull() || !out.emplace(id, i).second) // emplace の second: 入れられたか(すでにあれば false)
                {
                    out.clear();
                    return false;
                }
            }
            return true;
        }

        // 2体のアクターの記録が、同じ形(アクターの型、コンポーネントの数と、並んだ順の型)か。
        // whole のコンポーネントの先頭の部分が、part と同じ型の並びか(アクターの型も同じ)。part の方が長ければ false。
        bool StartsWithShape(const ActorEntries& whole, const ActorEntries& part)
        {
            if (TypeNameOf(*whole.actor) != TypeNameOf(*part.actor) || part.components.size() > whole.components.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < part.components.size(); ++i)
            {
                if (TypeNameOf(*whole.components[i]) != TypeNameOf(*part.components[i]))
                {
                    return false;
                }
            }
            return true;
        }

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

        // 3. プレハブから置いたアクターは、比べる相手(プレハブの中の、組になるオブジェクトの値)を決める。
        //    baseOf[番号] が nullptr でなければ、そのオブジェクトはプレハブとの違いだけを書く。
        //    プレハブそのものを書くとき(writeActorIdentity が false)は、違いではなく全部を書く。
        //    コンポーネントは、プレハブのコンポーネントと ID(Component::prefabComponentId)で組にする(PF-7a):
        //      - 組になったもの: 違いだけを書く
        //      - 組にならないもの(置いた後に足したもの): 全部の値を書く
        //      - プレハブにあるのに、組にならなかったもの(置いた後に外したもの): アクターの記録に "removedComponents" として ID を書く。
        //        「無いものは外した」と推し量らずに書いておくのは、プレハブに後から足したコンポーネント(これも置いたものに無い)と
        //        見分けるため。後から足したものは、読むときに置いたものにも足す(プレハブを直すと伝わる)。
        //    ID の無い古いプレハブは、今までどおり並び順で組にする(並びが違えば、全部の値を書く)。
        std::vector<const ArchiveValue*> baseOf(objects.size(), nullptr);
        std::unordered_map<std::size_t, ArchiveValue> removedOf; // アクターの番号 → 外したコンポーネントの ID の配列
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
            if (TypeNameOf(*base.actor) != actor.GetType().GetName())
            {
                result.warnings.push_back("アクター " + actor.name + ": 型がプレハブと違うので、違いではなく全部の値を書きます");
                continue;
            }
            const std::size_t componentCount = end - actorIndex - 1;
            std::unordered_map<core::Guid, std::size_t> prefabIndex;
            if (IndexPrefabComponents(base, prefabIndex))
            {
                // ID で組にする。1つのプレハブのコンポーネントと組になるのは1つだけ(同じ ID が2つあれば、2つ目は足したもの扱い)。
                std::vector<bool> used(base.components.size(), false);
                for (std::size_t c = 0; c < componentCount; ++c)
                {
                    // アクターの後ろに並ぶのは、そのアクターのコンポーネント(CollectObjects の並べ方)
                    const Component& component = static_cast<const Component&>(*objects[actorIndex + 1 + c]);
                    const auto found = prefabIndex.find(component.prefabComponentId);
                    if (component.prefabComponentId.IsNull() || found == prefabIndex.end() || used[found->second] ||
                        TypeNameOf(*base.components[found->second]) != component.GetType().GetName())
                    {
                        continue; // 足したもの: 全部の値を書く
                    }
                    used[found->second] = true;
                    baseOf[actorIndex + 1 + c] = base.components[found->second]->Find("properties");
                }
                ArchiveValue removed = ArchiveValue::MakeArray();
                for (std::size_t i = 0; i < base.components.size(); ++i)
                {
                    if (!used[i])
                    {
                        removed.Append(ArchiveValue(ComponentIdOf(*base.components[i]).ToString()));
                    }
                }
                if (removed.GetArraySize() > 0)
                {
                    removedOf.emplace(actorIndex, std::move(removed));
                }
            }
            else
            {
                // ID の無い古いプレハブ: 並び順で組にする。
                bool sameShape = base.components.size() == componentCount;
                for (std::size_t c = 0; sameShape && c < base.components.size(); ++c)
                {
                    sameShape = TypeNameOf(*base.components[c]) == objects[actorIndex + 1 + c]->GetType().GetName();
                }
                if (!sameShape)
                {
                    result.warnings.push_back("アクター " + actor.name + ": プレハブのコンポーネントに ID が無く、並びも違うので、"
                                              "違いではなく全部の値を書きます(プレハブを作り直すと ID が付きます)");
                    continue;
                }
                for (std::size_t c = 0; c < base.components.size(); ++c)
                {
                    baseOf[actorIndex + 1 + c] = base.components[c]->Find("properties");
                }
            }
            baseOf[actorIndex] = base.actor->Find("properties");
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
                // アクターの guid、prefab、components は、同じでも書く(この1体であること、元のプレハブ、番号の対応に要る)。
                // コンポーネントの prefabComponentId も、同じでも書く(読むときに、プレハブのどれと組にするかに要る)。
                static const std::vector<std::string_view> actorKeep = {"guid", "prefab", "components"};
                static const std::vector<std::string_view> componentKeep = {"prefabComponentId"};
                properties = DiffMembers(properties, *baseOf[i], objects[i]->GetType(), isActor ? actorKeep : componentKeep);
            }
            if (!options.writeActorIdentity && isActor)
            {
                properties.Remove("guid");
                properties.Remove("prefab");
            }
            if (!options.writeActorIdentity && !isActor)
            {
                // プレハブを書く: コンポーネントに、プレハブの中の新しい ID を振る(置いたものが、これで組になる)
                properties.Set("prefabComponentId", ArchiveValue(core::Guid::Generate().ToString()));
            }
            const auto removed = removedOf.find(i);
            if (removed != removedOf.end())
            {
                entry.Set("removedComponents", removed->second);
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
        // (アクター、コンポーネント): プレハブに後から足されたコンポーネント。2回目の後で、アクターの components の末尾に足す。
        std::vector<std::pair<core::ObjectHandle, core::ObjectHandle>> addedByPrefab;
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
            if (TypeNameOf(*file.actor) != TypeNameOf(*base.actor))
            {
                report.warnings.push_back(where + " と型が違うので、ファイルにある値だけで作ります");
                continue;
            }
            // コンポーネントの組の作り方: ファイルが ID を使っていて(コンポーネントの ID か、外した一覧がある)、
            // プレハブのコンポーネントに ID があれば ID で。そうでなければ(ID の無い古いファイルかプレハブ)並び順で。
            const ArchiveValue* removedValue = file.actor->Find("removedComponents");
            bool fileUsesIds = removedValue != nullptr;
            for (std::size_t c = 0; !fileUsesIds && c < file.components.size(); ++c)
            {
                fileUsesIds = !ComponentIdOf(*file.components[c]).IsNull();
            }
            std::unordered_map<core::Guid, std::size_t> prefabIndex;
            const bool byId = fileUsesIds && IndexPrefabComponents(base, prefabIndex);
            // 並び順で組にするときは、ファイルのコンポーネントが、プレハブのコンポーネントの「先頭の部分」と型が同じならよい
            // (ID の無い古いファイルを保存した後に、プレハブの末尾にコンポーネントを足した場合。COL-4 で、手元のシーンの
            // 跳ね回るものとプレイヤーのプレハブにコライダーを足したときに起きた)。残りは、プレハブに後から足されたものとして足す。
            if (!byId && !StartsWithShape(base, file))
            {
                report.warnings.push_back(where + " とコンポーネントの並びが違うので、ファイルにある値だけで作ります");
                continue;
            }
            Actor* actor = CreateFromPrefab(*prefab, *registry, types, report);
            if (actor == nullptr || actor->components.size() != base.components.size())
            {
                // 作りかけのものはシーンに入らないので、GC で回収される
                report.warnings.push_back(where + " から作れないので、ファイルにある値だけで作ります");
                continue;
            }
            handleOf[id] = actor->GetHandle();
            created.emplace_back(file.actor, actor);
            fromPrefab.insert(id);
            report.createdCount += 1 + actor->components.size();
            linked.emplace_back(actor->GetHandle(), prefab->GetHandle());

            // ファイルの c 番目のコンポーネントを、プレハブから作った i 番目と組にする(2回目で、その上にファイルの値を重ねる)。
            std::vector<bool> used(actor->components.size(), false);
            const auto pair = [&](std::size_t c, std::size_t i) {
                handleOf[file.componentIds[c]] = actor->components[i];
                created.emplace_back(file.components[c], registry->Resolve<Component>(actor->components[i]));
                fromPrefab.insert(file.componentIds[c]);
                used[i] = true;
            };
            if (!byId)
            {
                for (std::size_t c = 0; c < file.components.size(); ++c)
                {
                    pair(c, c);
                }
                for (std::size_t k = file.components.size(); k < actor->components.size(); ++k)
                {
                    addedByPrefab.emplace_back(actor->GetHandle(), actor->components[k]); // プレハブの末尾に後から足されたもの
                }
                continue;
            }
            for (std::size_t c = 0; c < file.components.size(); ++c)
            {
                const core::Guid componentId = ComponentIdOf(*file.components[c]);
                const auto found = componentId.IsNull() ? prefabIndex.end() : prefabIndex.find(componentId);
                if (found == prefabIndex.end() || used[found->second] ||
                    TypeNameOf(*file.components[c]) != TypeNameOf(*base.components[found->second]))
                {
                    // 置いた後に足したもの: 組にせず、1回目でファイルの値から作る
                    if (!componentId.IsNull())
                    {
                        report.warnings.push_back(where + ": コンポーネント " + componentId.ToString() +
                                                  " と組になるものがプレハブに無いので、ファイルにある値だけで作ります");
                    }
                    continue;
                }
                pair(c, found->second);
            }
            // 組にならなかったプレハブのコンポーネント: 外した一覧にあれば消し、無ければ(プレハブに後から足されたもの)残して足す。
            std::unordered_set<core::Guid> removed;
            for (std::size_t r = 0; removedValue != nullptr && r < removedValue->GetArraySize(); ++r)
            {
                core::Guid removedId;
                if (core::Guid::Parse(removedValue->At(r).AsString(), removedId))
                {
                    removed.insert(removedId);
                }
            }
            for (std::size_t k = 0; k < used.size(); ++k)
            {
                if (used[k])
                {
                    continue;
                }
                Component* component = registry->Resolve<Component>(actor->components[k]);
                if (component != nullptr && removed.count(component->prefabComponentId) > 0)
                {
                    component->Destroy(); // 外したもの(2回目でアクターの components がファイルの一覧に替わり、どこからも指されなくなる)
                }
                else
                {
                    addedByPrefab.emplace_back(actor->GetHandle(), actor->components[k]);
                }
            }
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
        // 2回目で、アクターの components はファイルの一覧に替わった。プレハブに後から足されたものを、その末尾に足す
        // (持ち主は、3. でシーンに入れるときに付け直される)。
        for (const auto& [actorHandle, componentHandle] : addedByPrefab)
        {
            if (Actor* actor = registry->Resolve<Actor>(actorHandle))
            {
                actor->components.push_back(componentHandle);
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
        // 親子が輪になっている(手で直したファイルなど)アクターは、親なしにする(HIER-2a)。
        // 輪のままだと、親をたどる計算が上限の段数まで回り、見える場所も意味をなさない。
        for (const core::ObjectHandle handle : report.addedActors)
        {
            Actor* actor = registry->Resolve<Actor>(handle);
            if (actor != nullptr && actor->GetParent() != nullptr && actor->IsDescendantOf(*actor))
            {
                actor->SetParent(nullptr, false); // 親なしにするのはいつも断られない
                report.warnings.push_back("アクター " + actor->name + " の親子が輪になっていたので、親なしにしました");
            }
        }
        return report;
    }
}
