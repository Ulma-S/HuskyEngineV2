// PhysicsScene2D.h の実装。
#include "engine/PhysicsScene2D.h"

#include "engine/Actor.h"
#include "engine/Collider2D.h"
#include "engine/Scene.h"

namespace husky::engine
{
    namespace
    {
        // ハンドルを、表の鍵にする整数にする(場所と世代を 64 ビットに並べる)。
        std::uint64_t KeyOf(core::ObjectHandle handle)
        {
            return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
        }

        // 置き直しが要るか(大きさ・ずらし・レイヤーなど、形の決め事が変わった)。
        bool SameShape(const ShapeDesc2D& a, const ShapeDesc2D& b)
        {
            return a.type == b.type && a.width == b.width && a.height == b.height && a.radius == b.radius &&
                   a.offset == b.offset && a.filter.layer == b.filter.layer && a.filter.mask == b.filter.mask &&
                   a.isTrigger == b.isTrigger;
        }
    }

    PhysicsScene2D::PhysicsScene2D()
        : m_world(CreatePhysicsWorld2D())
    {
    }

    void PhysicsScene2D::Update(Scene& scene, float dt)
    {
        Sync(scene);
        m_world->Step(dt);
        DetectOverlaps();
        Deliver(scene);
    }

    void PhysicsScene2D::Sync(Scene& scene)
    {
        for (auto& [key, entry] : m_entries)
        {
            entry.seen = false;
        }
        m_order.clear();
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        scene.ForEachActor([&](Actor& actor) {
            const Pose2D pose = MakePose2D(actor.transform);
            for (const core::ObjectHandle handle : actor.components)
            {
                const Collider2D* collider = registry != nullptr ? registry->Resolve<Collider2D>(handle) : nullptr;
                if (collider == nullptr || collider->IsPendingDestroy())
                {
                    continue;
                }
                const std::uint64_t key = KeyOf(handle);
                const ShapeDesc2D desc = collider->MakeShapeDesc();
                // [] は、鍵が無ければ新しい控えを作って返す(C# の Dictionary の TryAdd と取り出しを一度にするもの)
                Entry& entry = m_entries[key];
                if (entry.shape.IsNull())
                {
                    entry.collider = handle;
                    entry.shape = m_world->AddShape(desc, pose); // 新しいコライダー: 置く
                }
                else if (!SameShape(entry.desc, desc))
                {
                    m_world->RemoveShape(entry.shape); // 大きさなどが変わった: 置き直す
                    entry.shape = m_world->AddShape(desc, pose);
                }
                else
                {
                    m_world->SetPose(entry.shape, pose); // 場所を伝える
                }
                entry.actor = actor.GetHandle();
                entry.desc = desc;
                entry.pose = pose;
                entry.seen = true;
                m_order.push_back(key);
            }
        });
        // シーンに見つからなかったもの(消えたコライダー、消えたアクターのもの)を世界から外す。
        // 相手の側には、次の重なりの調べで「離れた」が届く。
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (!it->second.seen)
            {
                m_world->RemoveShape(it->second.shape);
                it = m_entries.erase(it); // erase は、消した次の位置を返す
            }
            else
            {
                ++it;
            }
        }
    }

    void PhysicsScene2D::DetectOverlaps()
    {
        m_events.clear();
        for (const std::uint64_t key : m_order)
        {
            Entry& entry = m_entries[key];
            // 自分の形に重なる形を探す(自分のマスクに当てはまるグループの形だけ)
            m_world->OverlapShape(entry.desc, entry.pose, entry.desc.filter.mask, m_hits);
            m_current.clear();
            for (const QueryHit2D& hit : m_hits)
            {
                const auto other = m_entries.find(KeyOf(hit.owner));
                if (hit.owner == entry.collider || other == m_entries.end() || other->second.actor == entry.actor)
                {
                    continue; // 自分自身、知らない形、同じアクターのほかのコライダーは数えない
                }
                if ((other->second.desc.filter.mask & entry.desc.filter.layer) == 0)
                {
                    continue; // 相手は自分のグループと当たらない(お互いに選んでいるときだけ当たる)
                }
                m_current.push_back(Contact{hit.owner, other->second.actor});
            }

            // 前のフレームと比べる: 今あって前に無い = 重なり始めた、前にあって今無い = 離れた
            for (const Contact& now : m_current)
            {
                bool before = false;
                for (const Contact& old : entry.contacts)
                {
                    before = before || old.collider == now.collider;
                }
                if (!before)
                {
                    m_events.push_back(OverlapEvent{true, entry.collider, now.collider, now.actor});
                }
            }
            for (const Contact& old : entry.contacts)
            {
                bool still = false;
                for (const Contact& now : m_current)
                {
                    still = still || now.collider == old.collider;
                }
                if (!still)
                {
                    m_events.push_back(OverlapEvent{false, entry.collider, old.collider, old.actor});
                }
            }
            entry.contacts.swap(m_current); // swap: 中身を入れ替える(コピーしない。前の入れ物は次のフレームで使い回す)
        }
    }

    void PhysicsScene2D::Deliver(Scene& scene)
    {
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            return;
        }
        for (const OverlapEvent& event : m_events)
        {
            const auto self = m_entries.find(KeyOf(event.selfCollider));
            Actor* actor = self != m_entries.end() ? registry->Resolve<Actor>(self->second.actor) : nullptr;
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            actor->OnOverlap(event);
            // アクターの全コンポーネントにも届ける(受け取った側がコンポーネントを足しても、添字で回すので崩れない)
            for (std::size_t i = 0; i < actor->components.size(); ++i)
            {
                Component* component = registry->Resolve<Component>(actor->components[i]);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    component->OnOverlap(event);
                }
            }
        }
    }
}
