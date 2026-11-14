// Scene.h の実装。
#include "engine/Scene.h"

#include <algorithm> // std::remove_if, std::sort

namespace husky::engine
{
    void Scene::Tick(double dt)
    {
        core::ObjectRegistry* registry = GetRegistry();
        if (registry == nullptr)
        {
            return;
        }

        // 1. 順番表を作る。フレームの最初の時点のアクターとコンポーネントだけが載る。
        //    そのため、Tick の途中で Spawn / AddComponent されたものは、次のフレームまで呼ばれない。
        //    clear は中身を空にするだけで、確保したメモリは手放さない(次のフレームで使い回せる)。
        m_tickEntries.clear();
        std::uint32_t sequence = 0;
        for (const core::ObjectHandle actorHandle : actors)
        {
            const Actor* actor = ResolveActor(actorHandle);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            // アクター自身の Tick。同じ Default のコンポーネントより先になるように、先に載せる。
            m_tickEntries.push_back({TickOrder::Default, sequence++, actorHandle, core::ObjectHandle{}, true});
            for (const core::ObjectHandle componentHandle : actor->components)
            {
                const Component* component = registry->Resolve<Component>(componentHandle);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    m_tickEntries.push_back({component->GetTickOrder(), sequence++, actorHandle, componentHandle, false});
                }
            }
        }

        // 2. 実行順で並べる。実行順が同じなら sequence(一覧の順)で並べるので、毎回同じ順番になる。
        //    std::sort に渡しているのはラムダ式で、「a を b より先にするなら true」を返す比べ方。
        std::sort(m_tickEntries.begin(), m_tickEntries.end(), [](const TickEntry& a, const TickEntry& b) {
            if (a.order != b.order)
            {
                return a.order < b.order;
            }
            return a.sequence < b.sequence;
        });

        // 3. 順番に呼ぶ。呼ぶ直前に毎回、Destroy されていないかを確かめる。
        //    前の行の Tick の中で、アクターやコンポーネントが Destroy されることがあるため。
        //    (順番表は Tick の中で書き換わらないので、範囲 for で回してよい。Spawn で変わるのは actors のほう)
        for (const TickEntry& entry : m_tickEntries)
        {
            Actor* actor = ResolveActor(entry.actor);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue; // アクターが Destroy されたら、そのコンポーネントも呼ばない
            }
            if (entry.isActor)
            {
                actor->Tick(dt);
                continue;
            }
            Component* component = registry->Resolve<Component>(entry.component);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                component->Tick(dt);
            }
        }
    }

    bool Scene::AddLoadedActor(Actor& actor)
    {
        core::ObjectRegistry* registry = GetRegistry();
        // 同じレジストリのものでなければ、ハンドルが別の表を指してしまうので入れない。
        if (registry == nullptr || registry->Resolve<Actor>(actor.GetHandle()) != &actor)
        {
            return false;
        }
        if (!actor.m_scene.IsNull()) // もうどこかのシーンにいる
        {
            return false;
        }
        actor.m_scene = GetHandle();
        if (actor.guid.IsNull()) // 古いファイルや、コマンドで作ったアクターには、まだ無い
        {
            actor.guid = core::Guid::Generate();
        }
        actor.RelinkComponentOwners();
        actors.push_back(actor.GetHandle());
        return true;
    }

    Actor* Scene::FindActorByGuid(const core::Guid& guid)
    {
        if (guid.IsNull())
        {
            return nullptr;
        }
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor != nullptr && !actor->IsPendingDestroy() && actor->guid == guid)
            {
                return actor;
            }
        }
        return nullptr;
    }

    std::size_t Scene::RemoveDestroyedActors()
    {
        // 先にコンポーネントを片付ける。
        // Destroy 済みのアクターは、そのコンポーネントもまとめて Destroy する(部品はアクターと一緒に消える)。
        // 生きているアクターは、Destroy 済みのコンポーネントを一覧から外す。
        // どちらも FlushDestroyed の前なので、同じフレームの終わりに破棄が確定する。
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor == nullptr)
            {
                continue;
            }
            if (actor->IsPendingDestroy())
            {
                actor->DestroyComponents();
                actor->DetachFromHierarchy(); // 親の子の一覧から外し、子は親なしにする(HIER-1。HIER-2 で子も一緒に消す)
            }
            else
            {
                actor->RemoveDestroyedComponents();
            }
        }

        const std::size_t before = actors.size();
        // Destroy 済みか、もう存在しないアクターのハンドルを、一覧から消す。
        actors.erase(std::remove_if(actors.begin(), actors.end(),
                                    [this](core::ObjectHandle handle) {
                                        const Actor* actor = ResolveActor(handle);
                                        return actor == nullptr || actor->IsPendingDestroy();
                                    }),
                     actors.end());
        return before - actors.size();
    }
}
