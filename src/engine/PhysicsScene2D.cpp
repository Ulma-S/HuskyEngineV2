// PhysicsScene2D.h の実装。
#include "engine/PhysicsScene2D.h"

#include "core/Profiler.h"

#include "engine/Actor.h"
#include "engine/Collider2D.h"
#include "engine/Scene.h"

namespace husky::engine
{
    namespace
    {
        // 場所・回転・拡大縮小が前のフレームとまったく同じか(同じなら、当たり判定の世界に伝えなくてよい)。
        bool SamePose(const Pose2D& a, const Pose2D& b)
        {
            return a.position == b.position && a.rotationDegrees == b.rotationDegrees && a.scale == b.scale;
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
        HUSKY_PROFILE_SCOPE("Physics2D"); // 全体(下の区間を含む)
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Sync");
            Sync(scene);
        }
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Step");
            m_world->Step(dt);
        }
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Detect");
            DetectOverlaps();
        }
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Deliver");
            Deliver(scene);
        }
    }

    void PhysicsScene2D::Sync(Scene& scene)
    {
        // 前のフレームに置いていたものに「まだ見ていない」の印を付ける(表をまるごと回さず、置いていたものだけ)。
        // swap: 2つの入れ物の中身を入れ替える(コピーしない。確保した領域も入れ替わるので、使い回せる)。
        m_previousOrder.swap(m_order);
        m_order.clear();
        for (const std::uint32_t index : m_previousOrder)
        {
            m_entries[index].seen = false;
        }
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
                const ShapeDesc2D desc = collider->MakeShapeDesc();
                // ハンドルの場所の番号を、そのまま配列の添字にする。足りなければ配列を伸ばす(温まった後は伸びない)。
                if (handle.index >= m_entries.size())
                {
                    m_entries.resize(static_cast<std::size_t>(handle.index) + 1);
                }
                Entry& entry = m_entries[handle.index];
                if (entry.collider != handle)
                {
                    // 空いている場所、または同じ場所の前の代(消えたコライダー)の控えが残っている: 前の形を外して置き直す
                    if (!entry.shape.IsNull())
                    {
                        m_world->RemoveShape(entry.shape);
                    }
                    entry.collider = handle;
                    entry.contacts.clear();
                    entry.shape = m_world->AddShape(desc, pose); // 新しいコライダー: 置く
                }
                else if (!SameShape(entry.desc, desc))
                {
                    m_world->RemoveShape(entry.shape); // 大きさなどが変わった: 置き直す
                    entry.shape = m_world->AddShape(desc, pose);
                }
                else if (!SamePose(entry.pose, pose))
                {
                    m_world->SetPose(entry.shape, pose); // 動いた: 場所を伝える(止まっているものは伝えない)
                }
                entry.actor = actor.GetHandle();
                entry.desc = desc;
                entry.pose = pose;
                entry.seen = true;
                m_order.push_back(handle.index);
            }
        });
        // シーンに見つからなかったもの(消えたコライダー、消えたアクターのもの)を世界から外し、場所を空ける。
        // 相手の側には、次の重なりの調べで「離れた」が届く。
        for (const std::uint32_t index : m_previousOrder)
        {
            Entry& entry = m_entries[index];
            if (!entry.seen && !entry.shape.IsNull())
            {
                m_world->RemoveShape(entry.shape);
                entry.collider = core::ObjectHandle{};
                entry.actor = core::ObjectHandle{};
                entry.shape = PhysicsShapeHandle2D{};
                entry.contacts.clear(); // 中身だけ消す(確保した領域は残り、次にこの場所を使うときに使い回す)
            }
        }
    }

    PhysicsScene2D::Entry* PhysicsScene2D::FindEntry(core::ObjectHandle collider)
    {
        if (collider.IsNull() || collider.index >= m_entries.size())
        {
            return nullptr;
        }
        Entry& entry = m_entries[collider.index];
        return entry.collider == collider ? &entry : nullptr; // 世代まで同じときだけ(同じ場所の別の代は別物)
    }

    void PhysicsScene2D::DetectOverlaps()
    {
        m_events.clear();
        for (const std::uint32_t index : m_order)
        {
            Entry& entry = m_entries[index];
            // 自分の形に重なる形を探す(自分のマスクに当てはまるグループの形だけ)
            m_world->OverlapShape(entry.desc, entry.pose, entry.desc.filter.mask, m_hits);
            m_current.clear();
            for (const QueryHit2D& hit : m_hits)
            {
                const Entry* other = FindEntry(hit.owner);
                if (hit.owner == entry.collider || other == nullptr || other->actor == entry.actor)
                {
                    continue; // 自分自身、知らない形、同じアクターのほかのコライダーは数えない
                }
                if ((other->desc.filter.mask & entry.desc.filter.layer) == 0)
                {
                    continue; // 相手は自分のグループと当たらない(お互いに選んでいるときだけ当たる)
                }
                m_current.push_back(Contact{hit.owner, other->actor});
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
            const Entry* self = FindEntry(event.selfCollider);
            Actor* actor = self != nullptr ? registry->Resolve<Actor>(self->actor) : nullptr;
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
