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
                   a.isTrigger == b.isTrigger && a.density == b.density && a.friction == b.friction &&
                   a.restitution == b.restitution;
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
        // このフレームに進める歩数を先に決める(Kinematic を動かす速さに使う)
        m_pendingSteps = m_stepper.Advance(dt);
        m_collisionEvents.clear(); // 同期(コライダーが消えた・置き直した)と進めたときに積む
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Sync");
            Sync(scene);
        }
        {
            HUSKY_PROFILE_SCOPE("Physics2D.Step");
            m_lastStepCount = m_pendingSteps;
            const float stepSeconds = static_cast<float>(m_stepper.GetStepSeconds());
            for (int i = 0; i < m_lastStepCount; ++i)
            {
                if (i == m_lastStepCount - 1)
                {
                    RecordPreviousPoses(); // 最後の 1 歩の前の場所(描くときに、ここから今の場所までを混ぜる)
                }
                m_world->Step(stepSeconds);
            }
            if (m_lastStepCount > 0)
            {
                WriteBackBodies(scene); // 物理が動かした分を、アクターへ戻す
                ProcessContactEvents(); // ぶつかり始めた・離れた
            }
            ApplyInterpolation(scene); // 描く位置を、前の歩と今の歩の間で混ぜる(進めなかったフレームでも、時間の分だけ進む)
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
            bool dynamic = false;
            const PhysicsBodyHandle2D body = SyncBody(actor, *registry, pose, dynamic);
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
                    if (!entry.collider.IsNull())
                    {
                        DropCollisionPairs(entry.collider); // 前の代がぶつかっていた相手に「離れた」
                    }
                    entry.collider = handle;
                    entry.contacts.clear();
                    place(); // 新しいコライダー: 置く
                }
                else if (entry.body != body || !SameShape(entry.desc, desc))
                {
                    // 付ける体が変わった(Rigidbody2D を足した・外した・体を置き直した)か、大きさなどが変わった: 置き直す。
                    // 重なっている相手(contacts)は残すので、置き直しただけで「離れた・重なった」は届かない。
                    // ぶつかりは Box2D が新しい形で調べ直すので、いったん「離れた」を届ける(次の歩でまたぶつかる)
                    if (!entry.shape.IsNull())
                    {
                        m_world->RemoveShape(entry.shape);
                    }
                    DropCollisionPairs(handle);
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
                entry.dynamic = dynamic;
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
                DropCollisionPairs(entry.collider); // ぶつかっていた相手に「離れた」(Box2D の「離れた」では持ち主が分からない)
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
                if (Actor* owner = registry->Resolve<Actor>(entry.actor))
                {
                    owner->ClearRenderTransform(); // 体が無くなったので、描くのも本当の transform に戻す
                }
                entry.rigidbody = core::ObjectHandle{};
                entry.actor = core::ObjectHandle{};
            }
        }
    }

    PhysicsBodyHandle2D PhysicsScene2D::SyncBody(const Actor& actor, core::ObjectRegistry& registry, const Pose2D& pose,
                                                 bool& dynamic)
    {
        // 最初の Rigidbody2D を探す(2 つ目からは使わない)
        Rigidbody2D* rigidbody = nullptr;
        core::ObjectHandle handle;
        for (const core::ObjectHandle component : actor.components)
        {
            Rigidbody2D* found = registry.Resolve<Rigidbody2D>(component);
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
            // 付いていた形は体と一緒に消えるので、コライダーはこの後の同期で新しい体に付け直される。
            // 同じ Rigidbody2D の置き直し(種類を変えたなど)なら、速さは引き継ぐ
            const bool sameRigidbody = entry.rigidbody == handle && !entry.body.IsNull();
            core::Vector2 velocity;
            float angularVelocity = 0.0f;
            if (sameRigidbody)
            {
                velocity = m_world->GetLinearVelocity(entry.body);
                angularVelocity = m_world->GetAngularVelocity(entry.body);
            }
            if (!entry.body.IsNull())
            {
                RemoveBody(entry);
            }
            entry.rigidbody = handle;
            entry.body = m_world->AddBody(desc, pose);
            entry.pose = pose;
            entry.previous = pose;
            entry.bodyRotation = m_world->GetBodyPose(entry.body).rotationDegrees;
            entry.moving = false;
            if (sameRigidbody)
            {
                m_world->SetLinearVelocity(entry.body, velocity);
                m_world->SetAngularVelocity(entry.body, angularVelocity);
            }
            // Rigidbody2D に体を貸す(速度の関数が使えるようになる。体ができる前に頼まれた速度はここで入る)
            rigidbody->ConnectBody(m_world.get(), entry.body);
        }
        else if (!SamePose(entry.pose, pose))
        {
            // アクターが、前に伝えた(書き戻した)場所から動いている = コードが動かした
            if (desc.type == BodyType2D::Kinematic && pose.scale == entry.pose.scale)
            {
                // Kinematic: 瞬間移動ではなく「このフレームの歩数で着く速さ」で動かす(通り道の Dynamic を押す)。
                // このフレームに 1 歩も進まないなら、まだ伝えない(次のフレームにまとめて動かす)
                if (m_pendingSteps > 0)
                {
                    m_world->MoveBodyTo(entry.body, pose, static_cast<float>(m_pendingSteps * m_stepper.GetStepSeconds()));
                    entry.pose = pose;
                    entry.previous = pose;
                    entry.moving = true; // 進めた後に、ぴったりの場所に合わせて止める(WriteBackBodies)
                }
            }
            else
            {
                // Dynamic と Static(と、拡大縮小が変わった Kinematic): 瞬間移動として伝える
                m_world->SetBodyPose(entry.body, pose);
                entry.pose = pose;
                entry.previous = pose; // 瞬間移動: 前の歩とは混ぜない(混ぜると、途中を通って見える)
                entry.bodyRotation = m_world->GetBodyPose(entry.body).rotationDegrees;
            }
        }
        entry.actor = actor.GetHandle();
        entry.desc = desc;
        entry.seen = true;
        dynamic = desc.type == BodyType2D::Dynamic;
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
            if (entry.moving)
            {
                // コードが動かした Kinematic: 着いた場所をぴったりに合わせ、速さを 0 に戻す
                // (そのままだと、アクターが止まっても体は同じ速さで進み続ける)。アクターはもうその場所にいる
                m_world->SetBodyPose(entry.body, entry.pose);
                m_world->SetLinearVelocity(entry.body, core::Vector2{});
                m_world->SetAngularVelocity(entry.body, 0.0f);
                entry.bodyRotation = m_world->GetBodyPose(entry.body).rotationDegrees;
                entry.moving = false;
                entry.previous = entry.pose; // コードが決めた場所なので、混ぜない
                continue;
            }
            if (entry.desc.type == BodyType2D::Static)
            {
                continue; // Static は動かない(アクターが場所を決める)
            }
            // Dynamic と、コードが動かしていない Kinematic(速度の関数で動かしたもの)は、体の場所をアクターに戻す
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
                if (!entry.desc.isTrigger && !other->desc.isTrigger && (entry.dynamic || other->dynamic))
                {
                    continue; // 物理で押し合う組: 重なりではなく、ぶつかり(OnCollision)で知らせる
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

    void PhysicsScene2D::RecordPreviousPoses()
    {
        for (const std::uint32_t index : m_bodyOrder)
        {
            BodyEntry& entry = m_bodies[index];
            if (entry.desc.type == BodyType2D::Static)
            {
                continue;
            }
            const Pose2D bodyPose = m_world->GetBodyPose(entry.body);
            entry.previous = entry.pose;
            entry.previous.position = bodyPose.position;
            // 角度はアクターの角度で持つ(書き戻しと同じく、体から最後に読んだ角度からの差を足す)
            entry.previous.rotationDegrees =
                entry.pose.rotationDegrees + std::remainder(bodyPose.rotationDegrees - entry.bodyRotation, 360.0f);
        }
    }

    void PhysicsScene2D::ApplyInterpolation(Scene& scene)
    {
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        if (registry == nullptr)
        {
            return;
        }
        // 貯まっている余り(次の 1 歩のどこまで来たか、0〜1)。0 なら previous、1 に近いほど pose
        const float alpha = static_cast<float>(m_stepper.GetAlpha());
        for (const std::uint32_t index : m_bodyOrder)
        {
            const BodyEntry& entry = m_bodies[index];
            Actor* actor = registry->Resolve<Actor>(entry.actor);
            if (actor == nullptr)
            {
                continue;
            }
            if (entry.desc.type == BodyType2D::Static || !SamePose(MakePose2D(actor->transform), entry.pose))
            {
                // 動かない体、またはコードが動かしてまだ体に伝えていない(0 歩のフレームの Kinematic)もの: そのまま描く
                actor->ClearRenderTransform();
                continue;
            }
            Transform render = actor->transform;
            render.position.x = entry.previous.position.x + (entry.pose.position.x - entry.previous.position.x) * alpha;
            render.position.y = entry.previous.position.y + (entry.pose.position.y - entry.previous.position.y) * alpha;
            render.rotation =
                entry.previous.rotationDegrees + (entry.pose.rotationDegrees - entry.previous.rotationDegrees) * alpha;
            actor->SetRenderTransform(render);
        }
    }

    void PhysicsScene2D::ProcessContactEvents()
    {
        m_world->TakeContactEvents(m_contactEvents);
        for (const ContactEvent2D& contact : m_contactEvents)
        {
            // 「離れた」で持ち主が分からないもの(形が消えた後)は、同期の DropCollisionPairs ですでに届けている
            const Entry* a = FindEntry(contact.ownerA);
            const Entry* b = FindEntry(contact.ownerB);
            if (a == nullptr || b == nullptr || a->actor == b->actor)
            {
                continue; // 知らない形、同じアクターの形どうし(同じ体に付いた形はぶつからないが、念のため)
            }
            // 組を探す(向きは問わない)
            std::size_t found = m_collisionPairs.size();
            for (std::size_t i = 0; i < m_collisionPairs.size(); ++i)
            {
                const CollisionPair& pair = m_collisionPairs[i];
                if ((pair.colliderA == a->collider && pair.colliderB == b->collider) ||
                    (pair.colliderA == b->collider && pair.colliderB == a->collider))
                {
                    found = i;
                    break;
                }
            }
            if (contact.begin)
            {
                if (found == m_collisionPairs.size())
                {
                    m_collisionPairs.push_back(CollisionPair{a->collider, b->collider, a->actor, b->actor});
                    PushCollision(true, m_collisionPairs.back(), contact.normal, contact.point);
                }
            }
            else if (found != m_collisionPairs.size())
            {
                const CollisionPair pair = m_collisionPairs[found];
                m_collisionPairs.erase(m_collisionPairs.begin() + static_cast<std::ptrdiff_t>(found));
                PushCollision(false, pair, core::Vector2{}, core::Vector2{});
            }
        }
    }

    void PhysicsScene2D::DropCollisionPairs(core::ObjectHandle collider)
    {
        for (std::size_t i = 0; i < m_collisionPairs.size();)
        {
            const CollisionPair pair = m_collisionPairs[i];
            if (pair.colliderA == collider || pair.colliderB == collider)
            {
                m_collisionPairs.erase(m_collisionPairs.begin() + static_cast<std::ptrdiff_t>(i));
                PushCollision(false, pair, core::Vector2{}, core::Vector2{});
            }
            else
            {
                ++i;
            }
        }
    }

    void PhysicsScene2D::PushCollision(bool begin, const CollisionPair& pair, const core::Vector2& normal,
                                       const core::Vector2& point)
    {
        // 両方に、自分の側から見た形で積む(B から見た向きは逆)
        PendingCollision forA;
        forA.selfActor = pair.actorA;
        forA.event = CollisionEvent{begin, pair.colliderA, pair.colliderB, pair.actorB, normal, point};
        PendingCollision forB;
        forB.selfActor = pair.actorB;
        forB.event = CollisionEvent{begin, pair.colliderB, pair.colliderA, pair.actorA, {-normal.x, -normal.y}, point};
        m_collisionEvents.push_back(forA);
        m_collisionEvents.push_back(forB);
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
        // ぶつかりのイベント(PHY-3)。届け先のアクターは、積んだときに覚えたもの(コライダーが消えた後の「離れた」でも届く)
        for (const PendingCollision& pending : m_collisionEvents)
        {
            Actor* actor = registry->Resolve<Actor>(pending.selfActor);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            actor->OnCollision(pending.event);
            for (std::size_t i = 0; i < actor->components.size(); ++i)
            {
                Component* component = registry->Resolve<Component>(actor->components[i]);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    component->OnCollision(pending.event);
                }
            }
        }
    }
}
