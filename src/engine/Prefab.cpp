// Prefab.h の実装。読み書きは SceneSerializer に任せ、ここでは「アクターが1つ」「guid を書かない」と、プレハブとの違いの集め方を決める。
#include "engine/Prefab.h"

#include "engine/ObjectSerializer.h"
#include "engine/SceneSerializer.h"

#include <algorithm> // std::find、std::any_of
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace husky::engine::Prefab
{
    namespace
    {
        const ArchiveValue* FindPrefabEntry(const ArchiveValue& prefab, std::int64_t id); // 中身は下
    }

    WriteResult Write(Actor& actor)
    {
        WriteResult result;
        World* scene = actor.GetWorld();
        if (scene == nullptr || actor.IsPendingDestroy())
        {
            result.warnings.push_back("アクターがシーンに入っていないか、Destroy 済みです");
            return result;
        }

        SceneSerializer::WriteOptions options;
        options.writeActorIdentity = false; // 置くたびに新しい guid を振り、元のプレハブは置いたときに書くため
        options.allowedOutside = actor.GetParentHandle(); // 根の親はプレハブの外: null と書くだけ(置いたものは親なし)
        std::vector<Actor*> targets = {&actor};
        actor.GetDescendants(targets); // 子孫も一緒に(親が先の順。HIER-2b)
        // 入れ子: 子孫のうち、根とは別のプレハブから置いたもの(そのプレハブが読み込まれているもの)は、
        // 元のプレハブとのつながりを保ち、違いだけを書く(HIER)
        core::ObjectRegistry* registry = scene->GetObjectRegistry();
        for (std::size_t i = 1; i < targets.size(); ++i)
        {
            const Actor& member = *targets[i];
            const PrefabAsset* source = registry != nullptr ? registry->Resolve<PrefabAsset>(member.prefab.asset) : nullptr;
            if (!member.prefab.IsNull() && member.prefab.guid != actor.prefab.guid && source != nullptr && source->IsLoaded())
            {
                options.nestedActors.push_back(&member);
            }
        }
        SceneSerializer::WriteResult saved = SceneSerializer::WriteActors(*scene, targets, options);
        result.value = std::move(saved.value);
        result.warnings = std::move(saved.warnings);
        // 入れ子の一まとまりの根(親が同じ入れ子のプレハブでないもの)に、このプレハブの中での ID を振る
        if (!options.nestedActors.empty())
        {
            const ArchiveValue& objects = *result.value.Find("objects");
            ArchiveValue newObjects = ArchiveValue::MakeArray();
            for (std::size_t i = 0; i < objects.GetArraySize(); ++i)
            {
                ArchiveValue entry = objects.At(i);
                const ArchiveValue* properties = entry.Find("properties");
                const ArchiveValue* nested = properties != nullptr ? properties->Find("prefab") : nullptr;
                if (nested != nullptr && nested->GetType() == ArchiveValue::Type::String)
                {
                    const ArchiveValue* parentNumber = properties->Find("parent");
                    const ArchiveValue* parent = parentNumber != nullptr && parentNumber->GetType() == ArchiveValue::Type::Int
                                                     ? FindPrefabEntry(result.value, parentNumber->AsInt())
                                                     : nullptr;
                    const ArchiveValue* parentProperties = parent != nullptr ? parent->Find("properties") : nullptr;
                    const ArchiveValue* parentPrefab = parentProperties != nullptr ? parentProperties->Find("prefab") : nullptr;
                    if (parentPrefab == nullptr || parentPrefab->AsString() != nested->AsString())
                    {
                        entry.Set("nestedId", ArchiveValue(core::Guid::Generate().ToString()));
                    }
                }
                newObjects.Append(std::move(entry));
            }
            result.value.Set("objects", std::move(newObjects));
        }
        result.ok = true;
        return result;
    }

    InstantiateResult Instantiate(const ArchiveValue& prefab, World& scene, const core::TypeRegistry& types)
    {
        InstantiateResult result;

        // 先に形を確かめる。読み始めてから断ると、作りかけのオブジェクトが残ってしまうため
        // (シーンに入らないので GC で回収はされるが、黙って作らないほうが分かりやすい)。
        const ArchiveValue* actors = prefab.Find("actors");
        if (actors == nullptr || actors->GetType() != ArchiveValue::Type::Array || actors->GetArraySize() == 0)
        {
            result.warnings.push_back("プレハブの \"actors\" に、アクターがありません");
            return result;
        }

        SceneSerializer::ReadReport report = SceneSerializer::Read(prefab, scene, types);
        result.warnings = std::move(report.warnings);
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        // 読んだ順("actors" の順)に並ぶので、最初が根
        for (std::size_t i = 0; registry != nullptr && i < report.addedActors.size(); ++i)
        {
            if (Actor* placed = registry->Resolve<Actor>(report.addedActors[i]))
            {
                result.actors.push_back(placed);
            }
        }
        if (!result.actors.empty() && report.addedActors.size() == actors->GetArraySize())
        {
            result.actor = result.actors[0];
        }
        return result;
    }

    InstantiateResult Instantiate(const PrefabAsset& prefab, World& scene, const core::TypeRegistry& types)
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
            // 子孫も、同じプレハブから置いたものとして覚える(HIER-2b。シーンには、それぞれプレハブの中の同じ ID のものとの違いを書く)
            for (Actor* placed : result.actors)
            {
                placed->prefab.guid = prefab.GetGuid();
                placed->prefab.asset = prefab.GetHandle(); // 強い参照: 置いたアクターがいる間、アセットも残る
            }
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

        // 型の既定の値の控え(型情報 → 書き出した値)。static: 最初に呼んだときに1回だけ作られ、プログラムの終わりまで残る
        // (ゲームスレッドだけで使う)。捨てるのは ClearDefaultPropertiesCache。
        std::unordered_map<const core::TypeInfo*, ArchiveValue>& DefaultPropertiesCache()
        {
            static std::unordered_map<const core::TypeInfo*, ArchiveValue> cache;
            return cache;
        }

        // 型の既定の値("properties" の値の木)。型から1つ作って書き出し、すぐに捨てる。型ごとに1回だけ作って控える
        // (既定の値は実行中に変わらないため)。作れない(登録簿に無い・メモリが足りない)なら nullptr。
        // プレハブのファイルに無い値は、置いたときに型の既定の値になるので、それと比べるのに使う。
        const ArchiveValue* FindDefaultProperties(const core::TypeInfo& type, const core::TypeRegistry& types,
                                                  core::ObjectRegistry& registry)
        {
            std::unordered_map<const core::TypeInfo*, ArchiveValue>& cache = DefaultPropertiesCache();
            const auto found = cache.find(&type);
            if (found != cache.end())
            {
                return found->second.Find("properties");
            }
            core::Object* temporary = types.Create(type.GetName(), registry);
            if (temporary == nullptr)
            {
                return nullptr;
            }
            ArchiveValue value = ObjectSerializer::Write(*temporary);
            temporary->Destroy(); // フレームの終わりに破棄される(シーンにもどこにも入れていない)
            return cache.emplace(&type, std::move(value)).first->second.Find("properties");
        }

        // 1つのオブジェクトの値(instance)を、プレハブの値(base)と比べ、違う値を out に足す。
        // 構造体は中まで比べ、道筋を点でつなぐ。ハンドルと guid は比べない。
        // プレハブに無い名前は、defaults(型の既定の値の、同じ段)と比べる(置いたときにその値になるため)。
        // defaults が無ければ飛ばす。
        void CompareMembers(const ArchiveValue& instance, const ArchiveValue& base, const ArchiveValue* defaults,
                            const core::TypeInfo& type, const std::string& component, const std::string& prefix,
                            std::vector<Override>& out)
        {
            for (std::size_t i = 0; i < instance.GetMemberCount(); ++i)
            {
                const std::string& name = instance.GetMemberName(i);
                const ArchiveValue& value = instance.GetMemberValue(i);
                const ArchiveValue* defaultValue = defaults != nullptr ? defaults->Find(name) : nullptr;
                const ArchiveValue* baseValue = base.Find(name);
                if (baseValue == nullptr)
                {
                    // プレハブのファイルに無い(コードで後から足したプロパティなど)。ただしアクターの prefab
                    // (どのプレハブから置いたか)は、プレハブのファイルにわざと書かない値なので比べない(guid と同じ扱い)。
                    const bool identity = component.empty() && prefix.empty() && name == "prefab";
                    baseValue = identity ? nullptr : defaultValue;
                }
                const core::Property* property = type.FindProperty(name.c_str());
                if (baseValue == nullptr || property == nullptr || property->GetType() == core::PropertyType::ObjectHandle ||
                    property->GetType() == core::PropertyType::ObjectHandleArray ||
                    property->GetType() == core::PropertyType::ObjectRef || property->IsReference() || // 参照の配列も比べない
                    property->GetType() == core::PropertyType::Guid)
                {
                    continue;
                }
                const std::string path = prefix + name;
                if (property->GetType() == core::PropertyType::Struct && value.GetType() == ArchiveValue::Type::Object &&
                    baseValue->GetType() == ArchiveValue::Type::Object)
                {
                    const ArchiveValue* structDefaults =
                        defaultValue != nullptr && defaultValue->GetType() == ArchiveValue::Type::Object ? defaultValue : nullptr;
                    CompareMembers(value, *baseValue, structDefaults, *property->GetStructType(), component, path + ".", out);
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

    namespace
    {
        // 記録の "properties" の中の GUID の文字列の値。無い・読めなければ空。
        core::Guid GuidOf(const ArchiveValue& entry, std::string_view name)
        {
            const ArchiveValue* properties = entry.Find("properties");
            const ArchiveValue* value = properties != nullptr ? properties->Find(name) : nullptr;
            core::Guid guid;
            if (value == nullptr || value->GetType() != ArchiveValue::Type::String || !core::Guid::Parse(value->AsString(), guid))
            {
                return core::Guid{};
            }
            return guid;
        }

        // 記録の親の番号(無ければ -1)。
        std::int64_t ParentNumberOf(const ArchiveValue& entry)
        {
            const ArchiveValue* properties = entry.Find("properties");
            const ArchiveValue* parent = properties != nullptr ? properties->Find("parent") : nullptr;
            return parent != nullptr && parent->GetType() == ArchiveValue::Type::Int ? parent->AsInt() : -1;
        }

        // プレハブの "actors" の中で、番号 rootNumber のアクターと、その子孫の番号を集める(親が先の順)。
        std::vector<std::int64_t> CollectSubtree(const ArchiveValue& data, std::int64_t rootNumber)
        {
            std::vector<std::int64_t> numbers = {rootNumber};
            const ArchiveValue* actors = data.Find("actors");
            // 1段ずつ広げる(プレハブは小さいので、毎回端から探してよい)。段数の上限で止める
            for (std::size_t first = 0; first < numbers.size() && numbers.size() < 4096; ++first)
            {
                for (std::size_t i = 0; actors != nullptr && i < actors->GetArraySize(); ++i)
                {
                    const std::int64_t number = actors->At(i).AsInt(-1);
                    const ArchiveValue* entry = FindPrefabEntry(data, number);
                    if (entry != nullptr && ParentNumberOf(*entry) == numbers[first] &&
                        std::find(numbers.begin(), numbers.end(), number) == numbers.end())
                    {
                        numbers.push_back(number);
                    }
                }
            }
            return numbers;
        }

        // 置いたもの actor について、足した子・消した子を result に入れる(HIER)。
        void FindChildChanges(const Actor& actor, const ArchiveValue& data, std::int64_t actorNumber, OverridesResult& result)
        {
            // 足した子: 直接の子のうち、このプレハブから来ていないもの
            std::vector<Actor*> children;
            actor.GetChildren(children);
            for (const Actor* child : children)
            {
                const bool fromPrefab = child->prefab.guid == actor.prefab.guid && !child->prefabActorId.IsNull() &&
                                        SceneSerializer::FindPrefabActorNumber(data, child->prefabActorId) >= 0;
                if (!fromPrefab)
                {
                    result.addedChildren.push_back({child->name, core::Guid{}, child->GetHandle()});
                }
            }
            // 消した子: プレハブでこのアクターの直接の子なのに、置いた一まとまり(同じプレハブの先祖をたどった根と、その子孫)に無いもの
            const Actor* root = &actor;
            for (const Actor* up = actor.GetParent(); up != nullptr && up->prefab.guid == actor.prefab.guid; up = up->GetParent())
            {
                root = up;
            }
            std::vector<Actor*> group = {const_cast<Actor*>(root)};
            root->GetDescendants(group);
            const ArchiveValue* actors = data.Find("actors");
            for (std::size_t i = 0; actors != nullptr && i < actors->GetArraySize(); ++i)
            {
                const ArchiveValue* entry = FindPrefabEntry(data, actors->At(i).AsInt(-1));
                if (entry == nullptr || ParentNumberOf(*entry) != actorNumber)
                {
                    continue;
                }
                const core::Guid childId = GuidOf(*entry, "prefabActorId");
                const bool present = std::any_of(group.begin(), group.end(), [&](const Actor* member) {
                    return member->prefab.guid == actor.prefab.guid && member->prefabActorId == childId;
                });
                if (!childId.IsNull() && !present)
                {
                    const ArchiveValue* name = entry->Find("properties")->Find("name");
                    result.removedChildren.push_back(
                        {name != nullptr ? name->AsString() : std::string(), childId, core::ObjectHandle{}});
                }
            }
        }
    }

    OverridesResult FindOverrides(const Actor& actor, const core::TypeRegistry* types)
    {
        OverridesResult result;
        const World* scene = actor.GetWorld();
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
        const std::int64_t actorNumber = SceneSerializer::FindPrefabActorNumber(data, actor.prefabActorId);
        const ArchiveValue* actorEntry = actorNumber >= 0 ? FindPrefabEntry(data, actorNumber) : nullptr;
        // プレハブの根から置いたものか(子は、位置や名前もプレハブとの違いとして扱う。perInstance にしない)
        const ArchiveValue* prefabActors = data.Find("actors");
        const bool isRoot = prefabActors != nullptr && prefabActors->GetArraySize() > 0 &&
                            prefabActors->At(0).AsInt(-1) == actorNumber;
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

        // プレハブのコンポーネントの記録は、型と properties がそろっていること
        for (const ArchiveValue* entry : prefabComponents)
        {
            if (entry == nullptr || entry->Find("type") == nullptr || entry->Find("properties") == nullptr)
            {
                result.error = "the prefab " + prefab->GetGuid().ToString() + " has a broken component";
                return result;
            }
        }

        // 置いたものの、生きているコンポーネント
        std::vector<const Component*> components;
        std::vector<std::size_t> rawIndexOf; // components[c] が actor.components の何番目か(コマンドが位置で指すため)
        for (std::size_t k = 0; k < actor.components.size(); ++k)
        {
            const Component* component = registry->Resolve<Component>(actor.components[k]);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                components.push_back(component);
                rawIndexOf.push_back(k);
            }
        }

        // 組を作る(PF-7b。保存と同じ決め方): pairOf[置いたものの位置] = プレハブの位置(組が無ければ prefabComponents.size())。
        //   - プレハブのコンポーネントに ID(prefabComponentId)があれば、ID と型が同じもの
        //   - ID の無い古いプレハブなら、並び順(数と型が同じときだけ。違えば比べられない)
        const std::size_t none = prefabComponents.size();
        std::vector<std::size_t> pairOf(components.size(), none);
        std::vector<bool> used(prefabComponents.size(), false);
        const auto prefabIdOf = [&prefabComponents](std::size_t i) {
            const ArchiveValue* id = prefabComponents[i]->Find("properties")->Find("prefabComponentId");
            core::Guid guid;
            return id != nullptr && core::Guid::Parse(id->AsString(), guid) ? guid : core::Guid{};
        };
        bool prefabHasIds = true;
        for (std::size_t i = 0; i < prefabComponents.size(); ++i)
        {
            prefabHasIds = prefabHasIds && !prefabIdOf(i).IsNull();
        }
        const auto typeOf = [&prefabComponents](std::size_t i) { return prefabComponents[i]->Find("type")->AsString(); };
        if (prefabHasIds)
        {
            for (std::size_t c = 0; c < components.size(); ++c)
            {
                for (std::size_t i = 0; i < prefabComponents.size() && pairOf[c] == none; ++i)
                {
                    if (!used[i] && !components[c]->prefabComponentId.IsNull() &&
                        prefabIdOf(i) == components[c]->prefabComponentId && typeOf(i) == components[c]->GetType().GetName())
                    {
                        pairOf[c] = i;
                        used[i] = true;
                    }
                }
            }
        }
        else
        {
            bool sameShape = components.size() == prefabComponents.size();
            for (std::size_t i = 0; sameShape && i < components.size(); ++i)
            {
                sameShape = typeOf(i) == components[i]->GetType().GetName();
            }
            if (!sameShape)
            {
                result.error = "the components differ from the prefab, and the prefab has no component ids "
                               "(make the prefab again to give them ids)";
                return result;
            }
            for (std::size_t i = 0; i < components.size(); ++i)
            {
                pairOf[i] = i;
                used[i] = true;
            }
        }

        result.prefab = prefab;
        // 型の既定の値(types を渡されたときだけ。プレハブのファイルに無い値と比べるため)
        core::ObjectRegistry* writableRegistry = scene->GetObjectRegistry();
        auto defaultsOf = [&](const core::TypeInfo& type) -> const ArchiveValue* {
            return types != nullptr ? FindDefaultProperties(type, *types, *writableRegistry) : nullptr;
        };
        ArchiveValue actorWritten = *ObjectSerializer::Write(actor).Find("properties");
        if (actor.IsTransformDriven())
        {
            actorWritten.Remove("transform"); // 計算の結果(UIActor)はプレハブとの違いに数えない(UI-5a)
        }
        CompareMembers(actorWritten, *actorProperties, defaultsOf(actor.GetType()),
                       actor.GetType(), std::string(), std::string(), result.overrides);
        if (!isRoot)
        {
            // 子の位置・回転・名前は親から見た値で、プレハブに決まった値がある。1体ずつ違うのが当たり前の値ではない
            for (Override& item : result.overrides)
            {
                item.perInstance = false;
            }
        }
        for (std::size_t c = 0; c < components.size(); ++c)
        {
            if (pairOf[c] == none)
            {
                // 置いた後に足したもの
                ComponentChange added;
                added.type = components[c]->GetType().GetName();
                added.index = rawIndexOf[c];
                for (std::size_t d = c; d > 0; --d)
                {
                    if (pairOf[d - 1] != none)
                    {
                        added.prefabIndex = pairOf[d - 1] + 1;
                        break;
                    }
                }
                result.addedComponents.push_back(std::move(added));
                continue;
            }
            const std::size_t first = result.overrides.size();
            CompareMembers(*ObjectSerializer::Write(*components[c]).Find("properties"),
                           *prefabComponents[pairOf[c]]->Find("properties"), defaultsOf(components[c]->GetType()),
                           components[c]->GetType(),
                           components[c]->GetType().GetName(), std::string(), result.overrides);
            for (std::size_t k = first; k < result.overrides.size(); ++k)
            {
                result.overrides[k].componentId = components[c]->prefabComponentId;
            }
        }
        for (std::size_t i = 0; i < prefabComponents.size(); ++i)
        {
            if (used[i])
            {
                continue;
            }
            // 置いた後に外したもの。戻す位置は、プレハブで前にあるもののうち、いちばん近い「組になったもの」のすぐ後ろ。
            // そうすると、プレハブの並び(=更新の順番)に近い形で戻る。前に組になったものが無ければ先頭。
            ComponentChange removed;
            removed.type = typeOf(i);
            removed.componentId = prefabIdOf(i);
            removed.prefabEntry = *prefabComponents[i];
            for (std::size_t j = i; j > 0 && removed.index == 0; --j)
            {
                for (std::size_t c = 0; c < components.size(); ++c)
                {
                    if (pairOf[c] == j - 1)
                    {
                        removed.index = rawIndexOf[c] + 1;
                        break;
                    }
                }
            }
            result.removedComponents.push_back(std::move(removed));
        }
        FindChildChanges(actor, data, actorNumber, result);
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
                      std::string& error, const core::Guid& prefabActorId)
    {
        const ArchiveValue* objects = data.Find("objects");
        const std::int64_t actorNumber = SceneSerializer::FindPrefabActorNumber(data, prefabActorId);
        if (objects == nullptr || actorNumber < 0)
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

        const std::size_t actorIndex = indexOf(actorNumber);
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
            // 書き込む記録: アクター自身か、ID が合うコンポーネント(ID が空なら、型の名前が合う最初のもの)
            std::size_t target = actorIndex;
            if (!change.component.empty())
            {
                target = entries.size();
                for (std::size_t i = 0; i < componentIds.GetArraySize() && target == entries.size(); ++i)
                {
                    const std::size_t index = indexOf(componentIds.At(i).AsInt(-1));
                    const ArchiveValue* type = index < entries.size() ? entries[index].Find("type") : nullptr;
                    const ArchiveValue* properties = index < entries.size() ? entries[index].Find("properties") : nullptr;
                    const ArchiveValue* id = properties != nullptr ? properties->Find("prefabComponentId") : nullptr;
                    const bool idMatches = change.componentId.IsNull() ||
                                           (id != nullptr && id->AsString() == change.componentId.ToString());
                    if (type != nullptr && type->AsString() == change.component && idMatches)
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
        const World* scene = actor.GetWorld();
        const core::ObjectRegistry* registry = scene != nullptr ? scene->GetObjectRegistry() : nullptr;
        std::size_t changed = 0;
        for (const ValueChange& change : changes)
        {
            // 書き換える相手: アクター自身か、ID が合う(生きている)コンポーネント(ID が空なら、型の名前が合う最初のもの)。
            // ID で指すので、置いた後に外したものには伝わらず、足した同じ型のものを取り違えることもない。
            core::Object* owner = change.component.empty() ? static_cast<core::Object*>(&actor) : nullptr;
            for (std::size_t i = 0; owner == nullptr && registry != nullptr && i < actor.components.size(); ++i)
            {
                Component* component = registry->Resolve<Component>(actor.components[i]);
                if (component != nullptr && !component->IsPendingDestroy() && component->GetType().GetName() == change.component &&
                    (change.componentId.IsNull() || component->prefabComponentId == change.componentId))
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

    namespace
    {
        // プレハブの値の木を、書き換えられる部品に分ける: objects の写し、アクターの記録の位置、components の番号の並び。
        // 読めなければ false と error。
        bool SplitPrefabData(const ArchiveValue& data, const core::Guid& prefabActorId, std::vector<ArchiveValue>& entries,
                             std::size_t& actorIndex, std::vector<std::int64_t>& componentIds, std::string& error)
        {
            const ArchiveValue* objects = data.Find("objects");
            const std::int64_t actorId = SceneSerializer::FindPrefabActorNumber(data, prefabActorId);
            if (objects == nullptr || actorId < 0)
            {
                error = "the prefab has no actor";
                return false;
            }
            actorIndex = objects->GetArraySize();
            for (std::size_t i = 0; i < objects->GetArraySize(); ++i)
            {
                entries.push_back(objects->At(i));
                const ArchiveValue* id = objects->At(i).Find("id");
                if (id != nullptr && id->AsInt(-1) == actorId)
                {
                    actorIndex = i;
                }
            }
            const ArchiveValue* properties = actorIndex < entries.size() ? entries[actorIndex].Find("properties") : nullptr;
            if (properties == nullptr)
            {
                error = "the prefab has no actor";
                return false;
            }
            const ArchiveValue* ids = properties->Find("components");
            for (std::size_t i = 0; ids != nullptr && i < ids->GetArraySize(); ++i)
            {
                componentIds.push_back(ids->At(i).AsInt(-1));
            }
            return true;
        }

        // SplitPrefabData で分けた部品から、値の木を組み立て直す(data の objects とアクターの components だけを差し替える)。
        void JoinPrefabData(const ArchiveValue& data, std::vector<ArchiveValue>& entries, std::size_t actorIndex,
                            const std::vector<std::int64_t>& componentIds, ArchiveValue& out)
        {
            ArchiveValue ids = ArchiveValue::MakeArray();
            for (const std::int64_t id : componentIds)
            {
                ids.Append(ArchiveValue(id));
            }
            ArchiveValue properties = *entries[actorIndex].Find("properties");
            properties.Set("components", std::move(ids));
            entries[actorIndex].Set("properties", std::move(properties));
            out = data;
            ArchiveValue& objects = out.Set("objects", ArchiveValue::MakeArray());
            for (ArchiveValue& entry : entries)
            {
                objects.Append(std::move(entry));
            }
        }
    }

    ArchiveValue MakeComponentEntry(const Component& component, const ArchiveValue& data, const core::Guid& componentId,
                                    std::vector<std::string>& warnings)
    {
        const Actor* owner = component.GetOwner();
        // 持ち主のプレハブの中のアクターの番号(HIER-2b。子のコンポーネントなら、その子の番号)
        const std::int64_t actorId =
            SceneSerializer::FindPrefabActorNumber(data, owner != nullptr ? owner->prefabActorId : core::Guid{});
        const World* scene = owner != nullptr ? owner->GetWorld() : nullptr;
        const core::ObjectRegistry* registry = scene != nullptr ? scene->GetObjectRegistry() : nullptr;
        // ハンドルは、持ち主のアクターを指すものだけを、プレハブのアクターの番号にする(Prefab::Write と同じ決まり)。
        ArchiveValue entry = ObjectSerializer::Write(component, [&](core::ObjectHandle handle) -> std::int64_t {
            if (handle.IsNull())
            {
                return -1;
            }
            if (owner != nullptr && handle == owner->GetHandle())
            {
                return actorId;
            }
            const core::Object* target = registry != nullptr ? registry->Resolve<core::Object>(handle) : nullptr;
            if (target != nullptr)
            {
                warnings.push_back(std::string(component.GetType().GetName()) + " points to a " + target->GetType().GetName() +
                                   " outside the actor; it is null in the prefab");
            }
            return -1;
        });
        ArchiveValue properties = entry.Find("properties") != nullptr ? *entry.Find("properties") : ArchiveValue::MakeObject();
        properties.Set("prefabComponentId", ArchiveValue(componentId.ToString()));
        entry.Set("properties", std::move(properties));
        return entry;
    }

    bool AddComponentToData(const ArchiveValue& data, const ArchiveValue& componentEntry, std::size_t position,
                            ArchiveValue& out, std::string& error, const core::Guid& prefabActorId)
    {
        std::vector<ArchiveValue> entries;
        std::size_t actorIndex = 0;
        std::vector<std::int64_t> componentIds;
        if (!SplitPrefabData(data, prefabActorId, entries, actorIndex, componentIds, error))
        {
            return false;
        }
        // 新しい番号: 今ある番号のいちばん大きいもの + 1(ファイルの中で重ならなければよい)
        std::int64_t newId = 0;
        for (const ArchiveValue& entry : entries)
        {
            const ArchiveValue* id = entry.Find("id");
            if (id != nullptr && id->AsInt(-1) >= newId)
            {
                newId = id->AsInt() + 1;
            }
        }
        ArchiveValue added = componentEntry;
        added.Set("id", ArchiveValue(newId));
        entries.push_back(std::move(added));
        const std::size_t at = position < componentIds.size() ? position : componentIds.size();
        componentIds.insert(componentIds.begin() + static_cast<std::ptrdiff_t>(at), newId);
        JoinPrefabData(data, entries, actorIndex, componentIds, out);
        return true;
    }

    std::vector<core::Guid> FindNestedPrefabs(const ArchiveValue& raw)
    {
        std::vector<core::Guid> found;
        const ArchiveValue* objects = raw.Find("objects");
        for (std::size_t i = 0; objects != nullptr && i < objects->GetArraySize(); ++i)
        {
            core::Guid guid = GuidOf(objects->At(i), "prefab");
            if (!guid.IsNull() && std::find(found.begin(), found.end(), guid) == found.end())
            {
                found.push_back(guid);
            }
        }
        return found;
    }

    ArchiveValue ExpandNested(const ArchiveValue& raw, core::ObjectRegistry& registry, const core::TypeRegistry& types,
                              const SceneSerializer::PrefabFinder& findPrefab, std::vector<std::string>& warnings)
    {
        if (FindNestedPrefabs(raw).empty())
        {
            return raw;
        }
        // 一時的なシーンに、シーンのファイルを読むのと同じ道具で作る(入れ子は元のプレハブから作って違いを重ねる)
        World* temporary = registry.Create<World>();
        if (temporary == nullptr)
        {
            warnings.push_back("not enough object memory to open nested prefabs");
            return raw;
        }
        SceneSerializer::ReadReport report = SceneSerializer::Read(raw, *temporary, types, findPrefab);
        for (std::string& warning : report.warnings)
        {
            warnings.push_back(std::move(warning));
        }
        // 入れ子の一まとまりの根の、このプレハブの中での ID("nestedId")。"actors" の i 番目 = 作った i 番目
        std::vector<std::pair<const Actor*, core::Guid>> nestedRoots;
        const ArchiveValue* actors = raw.Find("actors");
        for (std::size_t i = 0; actors != nullptr && i < actors->GetArraySize() && i < report.addedActors.size(); ++i)
        {
            const ArchiveValue* entry = FindPrefabEntry(raw, actors->At(i).AsInt(-1));
            const ArchiveValue* nestedId = entry != nullptr ? entry->Find("nestedId") : nullptr;
            core::Guid id;
            if (nestedId != nullptr && core::Guid::Parse(nestedId->AsString(), id))
            {
                nestedRoots.emplace_back(registry.Resolve<Actor>(report.addedActors[i]), id);
            }
        }
        // ID を、このプレハブの中の ID に直す(先に全部決めてから書く。根を探すのに prefab を使うため)
        std::vector<Actor*> created;
        std::vector<core::Guid> newIds;
        for (const core::ObjectHandle handle : report.addedActors)
        {
            Actor* actor = registry.Resolve<Actor>(handle);
            if (actor == nullptr)
            {
                continue;
            }
            created.push_back(actor);
            if (actor->prefab.IsNull())
            {
                newIds.push_back(actor->prefabActorId); // このプレハブのアクター: そのまま
                continue;
            }
            const Actor* root = actor;
            while (root->GetParent() != nullptr && root->GetParent()->prefab.guid == actor->prefab.guid)
            {
                root = root->GetParent();
            }
            core::Guid nestedId;
            for (const auto& [rootActor, id] : nestedRoots)
            {
                nestedId = rootActor == root ? id : nestedId;
            }
            if (nestedId.IsNull())
            {
                warnings.push_back("a nested prefab actor has no \"nestedId\" on its root (" + actor->name + ")");
            }
            newIds.push_back(actor == root ? nestedId : core::Guid::Combine(nestedId, actor->prefabActorId));
        }
        for (std::size_t i = 0; i < created.size(); ++i)
        {
            created[i]->prefabActorId = newIds[i];
            created[i]->prefab = {}; // 開いた形では、元のプレハブとのつながりは無い(全部の値を書く)
        }
        SceneSerializer::WriteOptions options;
        options.writeActorIdentity = false;
        options.keepPrefabIds = true;
        SceneSerializer::WriteResult written = SceneSerializer::WriteActors(*temporary, created, options);
        for (std::string& warning : written.warnings)
        {
            warnings.push_back(std::move(warning));
        }
        // 後片付け: 一時的なものは消す(どこからも指されないので、GC でも回収される)
        for (Actor* actor : created)
        {
            actor->Destroy();
        }
        temporary->RemoveDestroyedActors();
        temporary->Destroy();
        return std::move(written.value);
    }

    core::Guid FindNestedSource(const PrefabAsset& prefab, const core::Guid& prefabActorId)
    {
        const ArchiveValue& raw = prefab.GetRawData();
        const std::int64_t number = SceneSerializer::FindPrefabActorNumber(raw, prefabActorId);
        const ArchiveValue* entry = number >= 0 ? FindPrefabEntry(raw, number) : nullptr;
        return entry != nullptr ? GuidOf(*entry, "prefab") : core::Guid{};
    }

    std::vector<Actor*> InstantiateChild(const PrefabAsset& prefab, const core::Guid& childId, Actor& parent, World& scene,
                                         const core::TypeRegistry& types, std::string& error)
    {
        std::vector<Actor*> created;
        if (!prefab.IsLoaded())
        {
            error = "the prefab is not loaded";
            return created;
        }
        const ArchiveValue& data = prefab.GetData();
        const std::int64_t childNumber = SceneSerializer::FindPrefabActorNumber(data, childId);
        const ArchiveValue* actors = data.Find("actors");
        if (childNumber < 0 || childId.IsNull() || actors == nullptr || actors->At(0).AsInt(-1) == childNumber)
        {
            error = "the prefab has no child actor with id " + childId.ToString();
            return created;
        }
        // 子と子孫のアクターと、そのコンポーネントの記録だけで、小さなシーンの形を作って読む
        const std::vector<std::int64_t> numbers = CollectSubtree(data, childNumber);
        ArchiveValue objects = ArchiveValue::MakeArray();
        ArchiveValue actorNumbers = ArchiveValue::MakeArray();
        for (const std::int64_t number : numbers)
        {
            const ArchiveValue* entry = FindPrefabEntry(data, number);
            if (entry == nullptr)
            {
                continue;
            }
            ArchiveValue copy = *entry;
            if (number == childNumber)
            {
                ArchiveValue properties = *copy.Find("properties");
                properties.Remove("parent"); // 親は小さなシーンの外(この後で parent に付ける)
                copy.Set("properties", std::move(properties));
            }
            objects.Append(std::move(copy));
            actorNumbers.Append(ArchiveValue(number));
            const ArchiveValue* components = entry->Find("properties")->Find("components");
            for (std::size_t c = 0; components != nullptr && c < components->GetArraySize(); ++c)
            {
                if (const ArchiveValue* component = FindPrefabEntry(data, components->At(c).AsInt(-1)))
                {
                    objects.Append(*component);
                }
            }
        }
        ArchiveValue subset = ArchiveValue::MakeObject();
        subset.Set("version", ArchiveValue(SceneSerializer::FormatVersion));
        subset.Set("objects", std::move(objects));
        subset.Set("actors", std::move(actorNumbers));
        SceneSerializer::ReadReport report = SceneSerializer::Read(subset, scene, types);
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        for (const core::ObjectHandle handle : report.addedActors)
        {
            if (Actor* placed = registry != nullptr ? registry->Resolve<Actor>(handle) : nullptr)
            {
                placed->prefab.guid = prefab.GetGuid();
                placed->prefab.asset = prefab.GetHandle();
                created.push_back(placed);
            }
        }
        if (created.empty())
        {
            error = "could not create the child actor";
            return created;
        }
        created[0]->SetParent(&parent, false); // プレハブの中の値は、親から見た値
        return created;
    }

    bool AddChildToData(const ArchiveValue& data, const core::Guid& parentPrefabActorId, const Actor& child,
                        ArchiveValue& out, AddedChildIds& ids, std::vector<std::string>& warnings, std::string& error)
    {
        const std::int64_t parentNumber = SceneSerializer::FindPrefabActorNumber(data, parentPrefabActorId);
        const ArchiveValue* objects = data.Find("objects");
        const ArchiveValue* actors = data.Find("actors");
        if (parentNumber < 0 || objects == nullptr || actors == nullptr)
        {
            error = "the prefab has no actor with id " + parentPrefabActorId.ToString();
            return false;
        }
        // 書き込むもの: child と子孫(親が先の順)、それぞれの生きているコンポーネント
        std::vector<const Actor*> targets = {&child};
        {
            std::vector<Actor*> descendants;
            child.GetDescendants(descendants);
            targets.insert(targets.end(), descendants.begin(), descendants.end());
        }
        core::ObjectRegistry* registry = child.GetWorld() != nullptr ? child.GetWorld()->GetObjectRegistry() : nullptr;
        if (registry == nullptr)
        {
            error = "the actor is not in a scene";
            return false;
        }
        std::vector<const core::Object*> written;
        for (const Actor* actor : targets)
        {
            written.push_back(actor);
            for (const core::ObjectHandle handle : actor->components)
            {
                const Component* component = registry->Resolve<Component>(handle);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    written.push_back(component);
                }
            }
        }
        // 新しい番号: 今ある番号のいちばん大きいもの + 1 から
        std::int64_t nextNumber = 0;
        for (std::size_t i = 0; i < objects->GetArraySize(); ++i)
        {
            const ArchiveValue* id = objects->At(i).Find("id");
            if (id != nullptr && id->AsInt(-1) >= nextNumber)
            {
                nextNumber = id->AsInt() + 1;
            }
        }
        std::vector<std::pair<core::ObjectHandle, std::int64_t>> numberOf;
        for (const core::Object* object : written)
        {
            numberOf.emplace_back(object->GetHandle(), nextNumber++);
        }
        std::size_t outside = 0;
        // ハンドル → 番号。child の親はプレハブの親の番号、書き込むものは新しい番号、ほかは null
        const ObjectSerializer::HandleToId toId = [&](core::ObjectHandle handle) -> std::int64_t {
            if (handle.IsNull())
            {
                return -1;
            }
            if (handle == child.GetParentHandle())
            {
                return parentNumber;
            }
            for (const auto& [object, number] : numberOf)
            {
                if (object == handle)
                {
                    return number;
                }
            }
            ++outside;
            return -1;
        };
        ArchiveValue newObjects = *objects;
        ArchiveValue newActors = *actors;
        for (std::size_t i = 0; i < written.size(); ++i)
        {
            const core::Object& object = *written[i];
            const ArchiveValue value = ObjectSerializer::Write(object, toId);
            ArchiveValue properties = *value.Find("properties");
            const bool isActor = std::find(targets.begin(), targets.end(), &object) != targets.end();
            const core::Guid newId = core::Guid::Generate();
            if (isActor)
            {
                properties.Remove("guid");
                properties.Remove("prefab");
                properties.Set("prefabActorId", ArchiveValue(newId.ToString()));
                ids.actors.emplace_back(object.GetHandle(), newId);
                newActors.Append(ArchiveValue(numberOf[i].second));
            }
            else
            {
                properties.Set("prefabComponentId", ArchiveValue(newId.ToString()));
                ids.components.emplace_back(object.GetHandle(), newId);
            }
            ArchiveValue entry = ArchiveValue::MakeObject();
            entry.Set("id", ArchiveValue(numberOf[i].second));
            entry.Set("type", *value.Find("type"));
            entry.Set("properties", std::move(properties));
            newObjects.Append(std::move(entry));
        }
        if (outside > 0)
        {
            warnings.push_back("handles to objects outside the child actor were written as null (" + std::to_string(outside) + ")");
        }
        out = data;
        out.Set("objects", std::move(newObjects));
        out.Set("actors", std::move(newActors));
        return true;
    }

    bool RemoveChildFromData(const ArchiveValue& data, const core::Guid& childId, ArchiveValue& out, std::string& error)
    {
        const std::int64_t childNumber = SceneSerializer::FindPrefabActorNumber(data, childId);
        const ArchiveValue* objects = data.Find("objects");
        const ArchiveValue* actors = data.Find("actors");
        if (childId.IsNull() || childNumber < 0 || objects == nullptr || actors == nullptr ||
            actors->At(0).AsInt(-1) == childNumber)
        {
            error = "the prefab has no child actor with id " + childId.ToString();
            return false;
        }
        // 消す番号: 子と子孫のアクター、そのコンポーネント
        std::vector<std::int64_t> removed = CollectSubtree(data, childNumber);
        const std::size_t actorCount = removed.size();
        for (std::size_t a = 0; a < actorCount; ++a)
        {
            const ArchiveValue* entry = FindPrefabEntry(data, removed[a]);
            const ArchiveValue* components = entry != nullptr ? entry->Find("properties")->Find("components") : nullptr;
            for (std::size_t c = 0; components != nullptr && c < components->GetArraySize(); ++c)
            {
                removed.push_back(components->At(c).AsInt(-1));
            }
        }
        auto isRemoved = [&](std::int64_t number) {
            return std::find(removed.begin(), removed.end(), number) != removed.end();
        };
        ArchiveValue newObjects = ArchiveValue::MakeArray();
        for (std::size_t i = 0; i < objects->GetArraySize(); ++i)
        {
            const ArchiveValue* id = objects->At(i).Find("id");
            if (id == nullptr || !isRemoved(id->AsInt(-1)))
            {
                newObjects.Append(objects->At(i));
            }
        }
        ArchiveValue newActors = ArchiveValue::MakeArray();
        for (std::size_t i = 0; i < actors->GetArraySize(); ++i)
        {
            if (!isRemoved(actors->At(i).AsInt(-1)))
            {
                newActors.Append(actors->At(i));
            }
        }
        // 入れ子のプレハブの中の子を消すとき: ファイルの記録を消すだけだと、読むときに「入れ子のプレハブに後から足した子」として
        // 戻ってしまう。入れ子の一まとまりの根の "removedChildren" に、入れ子のプレハブの中の ID を書く(シーンと同じ)
        const ArchiveValue* childEntry = FindPrefabEntry(data, childNumber);
        const core::Guid nestedPrefab = childEntry != nullptr ? GuidOf(*childEntry, "prefab") : core::Guid{};
        if (!nestedPrefab.IsNull())
        {
            const ArchiveValue* root = childEntry;
            for (int depth = 0; depth < Actor::MaxHierarchyDepth; ++depth)
            {
                const ArchiveValue* parent = FindPrefabEntry(data, ParentNumberOf(*root));
                if (parent == nullptr || GuidOf(*parent, "prefab") != nestedPrefab)
                {
                    break;
                }
                root = parent;
            }
            if (root != childEntry)
            {
                const std::int64_t rootNumber = root->Find("id")->AsInt(-1);
                ArchiveValue patched = ArchiveValue::MakeArray();
                for (std::size_t i = 0; i < newObjects.GetArraySize(); ++i)
                {
                    ArchiveValue entry = newObjects.At(i);
                    if (entry.Find("id")->AsInt(-1) == rootNumber)
                    {
                        ArchiveValue list = entry.Find("removedChildren") != nullptr ? *entry.Find("removedChildren")
                                                                                     : ArchiveValue::MakeArray();
                        list.Append(ArchiveValue(GuidOf(*childEntry, "prefabActorId").ToString()));
                        entry.Set("removedChildren", std::move(list));
                    }
                    patched.Append(std::move(entry));
                }
                newObjects = std::move(patched);
            }
        }
        out = data;
        out.Set("objects", std::move(newObjects));
        out.Set("actors", std::move(newActors));
        return true;
    }

    bool RemoveComponentFromData(const ArchiveValue& data, const core::Guid& componentId, ArchiveValue& out,
                                 std::string& error, const core::Guid& prefabActorId)
    {
        std::vector<ArchiveValue> entries;
        std::size_t actorIndex = 0;
        std::vector<std::int64_t> componentIds;
        if (!SplitPrefabData(data, prefabActorId, entries, actorIndex, componentIds, error))
        {
            return false;
        }
        // ID(prefabComponentId)が合うコンポーネントの記録を探し、objects とアクターの components の両方から外す
        for (std::size_t c = 0; c < componentIds.size(); ++c)
        {
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const ArchiveValue* id = entries[i].Find("id");
                const ArchiveValue* properties = entries[i].Find("properties");
                const ArchiveValue* guid = properties != nullptr ? properties->Find("prefabComponentId") : nullptr;
                if (id != nullptr && id->AsInt(-1) == componentIds[c] && guid != nullptr &&
                    guid->AsString() == componentId.ToString())
                {
                    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                    if (i < actorIndex)
                    {
                        --actorIndex; // 前の記録を消したので、アクターの記録は1つ前へずれる
                    }
                    componentIds.erase(componentIds.begin() + static_cast<std::ptrdiff_t>(c));
                    // 入れ子のプレハブから置いたアクターなら、記録を消すだけだと読むときに戻ってしまうので、
                    // 外した一覧 "removedComponents" にも書く(シーンと同じ。HIER)
                    if (!GuidOf(entries[actorIndex], "prefab").IsNull())
                    {
                        ArchiveValue list = entries[actorIndex].Find("removedComponents") != nullptr
                                                ? *entries[actorIndex].Find("removedComponents")
                                                : ArchiveValue::MakeArray();
                        list.Append(ArchiveValue(componentId.ToString()));
                        entries[actorIndex].Set("removedComponents", std::move(list));
                    }
                    JoinPrefabData(data, entries, actorIndex, componentIds, out);
                    return true;
                }
            }
        }
        error = "the prefab has no component with id " + componentId.ToString();
        return false;
    }

    void ClearDefaultPropertiesCache()
    {
        DefaultPropertiesCache().clear();
    }
}
