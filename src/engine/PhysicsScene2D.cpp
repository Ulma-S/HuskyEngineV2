// PhysicsScene2D.h の実装。
#include "engine/PhysicsScene2D.h"

#include "core/Profiler.h"

#include "engine/Actor.h"
#include "engine/Collider2D.h"
#include "engine/Rigidbody2D.h"
#include "engine/Scene.h"

#include <cmath> // std::remainder

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

        // 体の置き直しが要るか(種類・重力の倍率など、体の決め事が変わった)。
        bool SameBody(const BodyDesc2D& a, const BodyDesc2D& b)
        {
            return a.type == b.type && a.gravityScale == b.gravityScale && a.linearDamping == b.linearDamping &&
                   a.angularDamping == b.angularDamping && a.fixedRotation == b.fixedRotation;
        }
    }

    PhysicsScene2D::PhysicsScene2D()
        : m_world(CreatePhysicsWorld2D())
    {
    }

    void PhysicsScene2D::Update(Scene& scene, double dt)
    {
        HUSKY_PROFILE_SCOPE("Physics2D"); // 全体(下の区間を含む)
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Sync");
            Sync(scene);
        }
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Step");
            m_lastStepCount = m_stepper.Advance(dt);
            const float stepSeconds = static_cast<float>(m_stepper.GetStepSeconds());
            for (int i = 0; i < m_lastStepCount; ++i)
            {
                m_world->Step(stepSeconds);
            }
            if (m_lastStepCount > 0)
            {
                WriteBackBodies(scene); // 物理が動かした分を、アクターへ戻す
            }
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
        m_previousBodyOrder.swap(m_bodyOrder);
        m_bodyOrder.clear();
        for (const std::uint32_t index : m_previousBodyOrder)
        {
            m_bodies[index].seen = false;
        }
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            return;
        }
        scene.ForEachActor([&](Actor& actor) {
            const Pose2D pose = MakePose2D(actor.transform);
            // Rigidbody2D があれば先に体を置く。コライダーはその体に付ける(無ければ null で、形だけの体に置く)
            const PhysicsBodyHandle2D body = SyncBody(actor, *registry, pose);
            for (const core::ObjectHandle handle : actor.components)
            {
                const Collider2D* collider = registry->Resolve<Collider2D>(handle);
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
                // 形を置く: 体があればその体に付け、無ければ形だけの体ごと置く
                auto place = [&] {
                    entry.body = body;
                    entry.shape = body.IsNull() ? m_world->AddShape(desc, pose) : m_world->AttachShape(body, desc);
                };
                if (entry.collider != handle)
                {
                    // 空いている場所、または同じ場所の前の代(消えたコライダー)の控えが残っている: 前の形を外して置き直す
                    if (!entry.shape.IsNull())
                    {
                        m_world->RemoveShape(entry.shape);
                    }
                    entry.collider = handle;
                    entry.contacts.clear();
                    place(); // 新しいコライダー: 置く
                }
                else if (entry.body != body || !SameShape(entry.desc, desc))
                {
                    // 付ける体が変わった(Rigidbody2D を足した・外した・体を置き直した)か、大きさなどが変わった: 置き直す。
                    // 重なっている相手(contacts)は残すので、置き直しただけで「離れた・重なった」は届かない
                    if (!entry.shape.IsNull())
                    {
                        m_world->RemoveShape(entry.shape);
                    }
                    place();
                }
                else if (body.IsNull() && !SamePose(entry.pose, pose))
                {
                    m_world->SetPose(entry.shape, pose); // 動いた: 場所を伝える(止まっているものは伝えない。体に付けた形は体と一緒に動く)
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
            if (!entry.seen && !entry.collider.IsNull())
            {
                if (!entry.shape.IsNull())
                {
                    m_world->RemoveShape(entry.shape);
                }
                entry.collider = core::ObjectHandle{};
                entry.actor = core::ObjectHandle{};
                entry.shape = PhysicsShapeHandle2D{};
                entry.body = PhysicsBodyHandle2D{};
                entry.contacts.clear(); // 中身だけ消す(確保した領域は残り、次にこの場所を使うときに使い回す)
            }
        }
        // 見つからなかった体(Rigidbody2D を外した、アクターが消えた)を外す。付いていた形は、上で外すか置き直した後
        for (const std::uint32_t index : m_previousBodyOrder)
        {
            BodyEntry& entry = m_bodies[index];
            if (!entry.seen && !entry.rigidbody.IsNull())
            {
                RemoveBody(entry);
                entry.rigidbody = core::ObjectHandle{};
                entry.actor = core::ObjectHandle{};
            }
        }
    }

    PhysicsBodyHandle2D PhysicsScene2D::SyncBody(const Actor& actor, core::ObjectRegistry& registry, const Pose2D& pose)
    {
        // 最初の Rigidbody2D を探す(2 つ目からは使わない)
        const Rigidbody2D* rigidbody = nullptr;
        core::ObjectHandle handle;
        for (const core::ObjectHandle component : actor.components)
        {
            const Rigidbody2D* found = registry.Resolve<Rigidbody2D>(component);
            if (found != nullptr && !found->IsPendingDestroy())
            {
                rigidbody = found;
                handle = component;
                break;
            }
        }
        if (rigidbody == nullptr)
        {
            return PhysicsBodyHandle2D{};
        }
        if (handle.index >= m_bodies.size())
        {
            m_bodies.resize(static_cast<std::size_t>(handle.index) + 1);
        }
        BodyEntry& entry = m_bodies[handle.index];
        const BodyDesc2D desc = rigidbody->MakeBodyDesc();
        if (entry.rigidbody != handle || !SameBody(entry.desc, desc))
        {
            // 新しい Rigidbody2D(または同じ場所の前の代の控え)か、決め事が変わった: 体を置き直す。
            // 付いていた形は体と一緒に消えるので、コライダーはこの後の同期で新しい体に付け直される
            if (!entry.body.IsNull())
            {
                RemoveBody(entry);
            }
            entry.rigidbody = handle;
            entry.body = m_world->AddBody(desc, pose);
            entry.pose = pose;
            entry.bodyRotation = m_world->GetBodyPose(entry.body).rotationDegrees;
        }
        else if (!SamePose(entry.pose, pose))
        {
            // アクターが、前に伝えた(書き戻した)場所から動いている = コードが動かした: 瞬間移動として伝える
            m_world->SetBodyPose(entry.body, pose);
            entry.pose = pose;
            entry.bodyRotation = m_world->GetBodyPose(entry.body).rotationDegrees;
        }
        entry.actor = actor.GetHandle();
        entry.desc = desc;
        entry.seen = true;
        m_bodyOrder.push_back(handle.index);
        return entry.body;
    }

    void PhysicsScene2D::RemoveBody(BodyEntry& entry)
    {
        // この体に付いていたコライダーの控えは、形が体と一緒に消えるので「置いていない」にする
        // (付いていたのは前のフレームに置いたものだけなので、前の一覧を見れば足りる)
        for (const std::uint32_t index : m_previousOrder)
        {
            Entry& collider = m_entries[index];
            if (collider.body == entry.body)
            {
                collider.shape = PhysicsShapeHandle2D{};
                collider.body = PhysicsBodyHandle2D{};
            }
        }
        m_world->RemoveBody(entry.body);
        entry.body = PhysicsBodyHandle2D{};
    }

    void PhysicsScene2D::WriteBackBodies(Scene& scene)
    {
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            return;
        }
        for (const std::uint32_t index : m_bodyOrder)
        {
            BodyEntry& entry = m_bodies[index];
            if (entry.desc.type != BodyType2D::Dynamic)
            {
                continue; // Kinematic と Static は、アクターが場所を決める(アクター → Box2D だけ)
            }
            Actor* actor = registry->Resolve<Actor>(entry.actor);
            if (actor == nullptr)
            {
                continue;
            }
            const Pose2D bodyPose = m_world->GetBodyPose(entry.body);
            actor->transform.position.x = bodyPose.position.x;
            actor->transform.position.y = bodyPose.position.y;
            // Box2D の角度は -180〜180 度に丸められ、近似の誤差もある。アクターの角度(370 度など)をそのまま保つため、
            // 前に体から読んだ角度からの差(-180〜180 に丸める)だけを足す。回っていなければ差は 0 で、角度は変わらない
            const float delta = std::remainder(bodyPose.rotationDegrees - entry.bodyRotation, 360.0f);
            if (delta != 0.0f)
            {
                actor->transform.rotation = entry.pose.rotationDegrees + delta;
                entry.bodyRotation = bodyPose.rotationDegrees;
            }
            entry.pose = MakePose2D(actor->transform); // 書き戻した場所を覚える(次の同期で、コードが動かしたかを見分ける)
        }
        // 体に付けたコライダーの控えの場所も合わせる(この後の重なりの調べで使う)
        for (const std::uint32_t index : m_order)
        {
            Entry& entry = m_entries[index];
            if (entry.body.IsNull())
            {
                continue;
            }
            const Actor* actor = registry->Resolve<Actor>(entry.actor);
            if (actor != nullptr)
            {
                entry.pose = MakePose2D(actor->transform);
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
