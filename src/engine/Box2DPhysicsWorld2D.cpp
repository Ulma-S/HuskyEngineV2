// PhysicsWorld2D の中身: Box2D(COL-2)。Box2D の型と関数を使うのは、このファイル(と PhysicsMemory.cpp)だけ。
// ここを別の中身(自作、ほかのライブラリ)に替えても、PhysicsWorld2D.h を使う側は変わらない(設計メモ「当たり判定と物理」)。
//
// Box2D の考え方(Unity の Rigidbody2D と Collider2D にあたる):
//   - 世界(b2World)の中に、体(b2Body。位置と回転を持つ)を置き、体に形(b2Shape。四角や円)を付ける。
//   - 体には種類がある: 動かない(static)、コードが動かす(kinematic)、物理で動く(dynamic)。
//     当たり判定だけの形(AddShape)は「コードが動かす体」に1つずつ付ける(kinematic 同士は押し合わない)。
//     物理で動かすもの(PHY-1)は、AddBody で体を置き、AttachShape で形をいくつも付ける。
//   - ID(b2BodyId、b2ShapeId)で指す。世代付きなので、消した後の古い ID は無効と分かる(エンジンのハンドルと同じ考え方)。
//
// 拡大縮小: Box2D の形は拡大縮小を持たないので、拡大した大きさで形を作り直す(SetPose で拡大が変わったときだけ)。
// 動作スレッド: ゲームスレッドのみ
#include "engine/PhysicsWorld2D.h"

#include "engine/PhysicsMemory.h"

#include <box2d/box2d.h>

#include <algorithm>     // std::max、std::remove
#include <cmath>         // std::fabs
#include <unordered_map>

namespace husky::engine
{
    Pose2D MakePose2D(const Transform& transform)
    {
        Pose2D pose;
        pose.position = {transform.position.x, transform.position.y};
        pose.rotationDegrees = transform.rotation;
        pose.scale = {transform.scale.x, transform.scale.y};
        return pose;
    }

    namespace
    {
        constexpr float Pi = 3.14159265358979f;

        // Box2D は「メートル」で考えて作られている(0.1〜10 メートルの物がいちばん安定する)。
        // このエンジンの 1 単位 = 1 ピクセルなので、100 単位を 1 メートルとして扱わせる(Unity の Pixels Per Unit の既定 100 と同じ)。
        // 当たりの許容の誤差などが、この値に合わせて大きくなる。世界を作る前に1回だけ設定する(Box2D 全体の設定)。
        constexpr float UnitsPerMeter = 100.0f;

        b2Rot ToRot(float degrees)
        {
            return b2MakeRot(degrees * Pi / 180.0f);
        }

        float FromRot(b2Rot rotation)
        {
            return b2Rot_GetAngle(rotation) * 180.0f / Pi; // -180 〜 180 度
        }

        b2BodyType ToBodyType(BodyType2D type)
        {
            switch (type)
            {
            case BodyType2D::Static:
                return b2_staticBody;
            case BodyType2D::Kinematic:
                return b2_kinematicBody;
            case BodyType2D::Dynamic:
                break;
            }
            return b2_dynamicBody;
        }

        // 自前のレイヤーとマスク(32 ビット)を、Box2D の印(64 ビット)にする。下の 32 ビットだけを使う。
        b2Filter ToFilter(const CollisionFilter& filter)
        {
            b2Filter result = b2DefaultFilter();
            result.categoryBits = filter.layer;
            result.maskBits = filter.mask;
            return result;
        }

        // 問い合わせの印: 自分はすべてのグループに属する(どの形のマスクにも当てはまる)とし、mask のグループの形だけを探す。
        b2QueryFilter ToQueryFilter(std::uint32_t mask)
        {
            b2QueryFilter result = b2DefaultQueryFilter();
            result.categoryBits = AllCollisionLayers;
            result.maskBits = mask;
            return result;
        }

        // 形の大きさの下限(ワールドの単位)。拡大縮小 0 や大きさ 0 でも、面積 0 の形を作らない(面積 0 の形は、物理を足したときに
        // 重さの計算で 0 で割ることになり、おかしな値になる)。とても小さいので、ふつうは当たらない。
        constexpr float MinHalfExtent = 0.01f;

        // 円の拡大: 縦横で違うときは、大きいほうに合わせる(円のまま。Unity と同じ)。
        float CircleScale(const core::Vector2& scale)
        {
            return std::max(std::fabs(scale.x), std::fabs(scale.y));
        }

        // 拡大した四角(体のローカルの座標。offset も拡大する)。
        b2Polygon MakeScaledBox(const ShapeDesc2D& desc, const core::Vector2& scale)
        {
            // 大きさは絶対値(マイナスの大きさ・拡大でも、角の並びが逆にならないように)。下限より小さくしない
            const float halfWidth = std::max(std::fabs(desc.width * scale.x) * 0.5f, MinHalfExtent);
            const float halfHeight = std::max(std::fabs(desc.height * scale.y) * 0.5f, MinHalfExtent);
            const b2Vec2 center{desc.offset.x * scale.x, desc.offset.y * scale.y};
            return b2MakeOffsetBox(halfWidth, halfHeight, center, b2Rot_identity);
        }

        // 拡大した円(体のローカルの座標)。
        b2Circle MakeScaledCircle(const ShapeDesc2D& desc, const core::Vector2& scale)
        {
            b2Circle circle;
            circle.center = b2Vec2{desc.offset.x * scale.x, desc.offset.y * scale.y};
            circle.radius = std::max(std::fabs(desc.radius) * CircleScale(scale), MinHalfExtent);
            return circle;
        }

        // 問い合わせ用に、desc の形を pose に置いたときの「ワールドの形」(Box2D の ShapeProxy: 点の集まり + 丸みの半径)を作る。
        b2ShapeProxy MakeWorldProxy(const ShapeDesc2D& desc, const Pose2D& pose)
        {
            const b2Vec2 position{pose.position.x, pose.position.y};
            const b2Rot rotation = ToRot(pose.rotationDegrees);
            if (desc.type == ShapeType2D::Circle)
            {
                const b2Circle circle = MakeScaledCircle(desc, pose.scale);
                return b2MakeOffsetProxy(&circle.center, 1, circle.radius, position, rotation); // 点 1 つ + 半径 = 円
            }
            const b2Polygon box = MakeScaledBox(desc, pose.scale);
            return b2MakeOffsetProxy(box.vertices, box.count, 0.0f, position, rotation); // 四隅 = 四角
        }

        // b2World_OverlapShape / OverlapAABB が、見つけた形ごとに呼ぶ関数に渡す入れ物。
        struct QueryContext
        {
            std::vector<b2ShapeId>* found = nullptr;
            const b2Vec2* point = nullptr; // 点の問い合わせなら、その点(範囲で見つけた後、本当に中かを調べる)
        };

        bool CollectShape(b2ShapeId shapeId, void* context)
        {
            QueryContext& query = *static_cast<QueryContext*>(context);
            if (query.point == nullptr || b2Shape_TestPoint(shapeId, *query.point))
            {
                query.found->push_back(shapeId);
            }
            return true; // true: 続けて探す
        }
    }

    class Box2DPhysicsWorld2D final : public PhysicsWorld2D
    {
    public:
        Box2DPhysicsWorld2D()
        {
            InstallPhysicsMemoryHooks();               // Box2D の確保を MemoryTracker に通す
            b2SetLengthUnitsPerMeter(UnitsPerMeter);   // 1 メートル = 100 単位
            b2WorldDef worldDef = b2DefaultWorldDef();
            worldDef.gravity = b2Vec2{0.0f, 0.0f};     // 重力はシーン・ゲームが SetGravity で決める(このゲームは上から見る形で 0)
            m_world = b2CreateWorld(&worldDef);
        }

        ~Box2DPhysicsWorld2D() override
        {
            // 世界を消すと、中の体と形もまとめて消える
            b2DestroyWorld(m_world);
        }

        // コピーしない(世界を2回消してしまうため)。
        Box2DPhysicsWorld2D(const Box2DPhysicsWorld2D&) = delete;
        Box2DPhysicsWorld2D& operator=(const Box2DPhysicsWorld2D&) = delete;

        PhysicsShapeHandle2D AddShape(const ShapeDesc2D& desc, const Pose2D& pose) override
        {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_kinematicBody; // コードが動かす体(当たり判定だけのもの)
            bodyDef.position = b2Vec2{pose.position.x, pose.position.y};
            bodyDef.rotation = ToRot(pose.rotationDegrees);
            const b2BodyId body = b2CreateBody(m_world, &bodyDef);
            const b2ShapeId shape = CreateScaledShape(body, desc, pose.scale);

            // Box2D の形の ID を、整数 1 つにしまう(b2StoreShapeId。0 にはならない)。これを外へのハンドルにする
            PhysicsShapeHandle2D handle;
            handle.id = b2StoreShapeId(shape);
            ShapeEntry entry;
            entry.desc = desc;
            entry.body = body;
            entry.scale = pose.scale;
            entry.ownsBody = true;
            m_entries[handle.id] = entry;
            return handle;
        }

        void SetPose(PhysicsShapeHandle2D shape, const Pose2D& pose) override
        {
            const auto found = m_entries.find(shape.id);
            if (found == m_entries.end() || !found->second.ownsBody)
            {
                return; // 知らない形、または体に付けた形(体と一緒に動くので、形だけは動かさない)
            }
            ShapeEntry& entry = found->second;
            b2Body_SetTransform(entry.body, b2Vec2{pose.position.x, pose.position.y}, ToRot(pose.rotationDegrees));
            if (pose.scale != entry.scale)
            {
                // 拡大縮小が変わった: 形を作り直す(Box2D の形は拡大縮小を持たないため)
                RescaleShape(b2LoadShapeId(shape.id), entry.desc, pose.scale);
                entry.scale = pose.scale;
            }
        }

        void RemoveShape(PhysicsShapeHandle2D shape) override
        {
            const auto found = m_entries.find(shape.id);
            if (found == m_entries.end())
            {
                return;
            }
            if (found->second.ownsBody)
            {
                b2DestroyBody(found->second.body); // 体を消すと、付いている形も消える
            }
            else
            {
                // 体に付けた形: 形だけを外す(true: 体の重さを計算し直す)。体の控えの一覧からも外す
                b2DestroyShape(b2LoadShapeId(shape.id), true);
                const auto body = m_bodies.find(b2StoreBodyId(found->second.body));
                if (body != m_bodies.end())
                {
                    std::vector<std::uint64_t>& shapes = body->second.shapes;
                    shapes.erase(std::remove(shapes.begin(), shapes.end(), shape.id), shapes.end());
                }
            }
            m_entries.erase(found);
        }

        PhysicsBodyHandle2D AddBody(const BodyDesc2D& desc, const Pose2D& pose) override
        {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = ToBodyType(desc.type);
            bodyDef.position = b2Vec2{pose.position.x, pose.position.y};
            bodyDef.rotation = ToRot(pose.rotationDegrees);
            bodyDef.gravityScale = desc.gravityScale;
            bodyDef.linearDamping = desc.linearDamping;
            bodyDef.angularDamping = desc.angularDamping;
            bodyDef.fixedRotation = desc.fixedRotation;
            const b2BodyId body = b2CreateBody(m_world, &bodyDef);

            PhysicsBodyHandle2D handle;
            handle.id = b2StoreBodyId(body);
            BodyEntry entry;
            entry.body = body;
            entry.scale = pose.scale;
            m_bodies[handle.id] = entry;
            return handle;
        }

        PhysicsShapeHandle2D AttachShape(PhysicsBodyHandle2D body, const ShapeDesc2D& desc) override
        {
            const auto found = m_bodies.find(body.id);
            if (found == m_bodies.end())
            {
                return PhysicsShapeHandle2D{};
            }
            BodyEntry& bodyEntry = found->second;
            const b2ShapeId shape = CreateScaledShape(bodyEntry.body, desc, bodyEntry.scale);
            PhysicsShapeHandle2D handle;
            handle.id = b2StoreShapeId(shape);
            ShapeEntry entry;
            entry.desc = desc;
            entry.body = bodyEntry.body;
            entry.scale = bodyEntry.scale;
            entry.ownsBody = false;
            m_entries[handle.id] = entry;
            bodyEntry.shapes.push_back(handle.id);
            return handle;
        }

        void RemoveBody(PhysicsBodyHandle2D body) override
        {
            const auto found = m_bodies.find(body.id);
            if (found == m_bodies.end())
            {
                return;
            }
            for (const std::uint64_t shapeId : found->second.shapes)
            {
                m_entries.erase(shapeId); // 付いている形の控えも消す(Box2D の形は、体と一緒に消える)
            }
            b2DestroyBody(found->second.body);
            m_bodies.erase(found);
        }

        std::size_t GetBodyCount() const override
        {
            return m_bodies.size();
        }

        void SetBodyPose(PhysicsBodyHandle2D body, const Pose2D& pose) override
        {
            BodyEntry* entry = FindBody(body);
            if (entry == nullptr)
            {
                return;
            }
            b2Body_SetTransform(entry->body, b2Vec2{pose.position.x, pose.position.y}, ToRot(pose.rotationDegrees));
            b2Body_SetAwake(entry->body, true); // 眠っていた体も、動かされたら起こす(周りとの押し合いを調べ直す)
            if (pose.scale != entry->scale)
            {
                for (const std::uint64_t shapeId : entry->shapes)
                {
                    ShapeEntry& shape = m_entries[shapeId];
                    RescaleShape(b2LoadShapeId(shapeId), shape.desc, pose.scale);
                    shape.scale = pose.scale;
                }
                entry->scale = pose.scale;
            }
        }

        Pose2D GetBodyPose(PhysicsBodyHandle2D body) const override
        {
            Pose2D pose;
            const BodyEntry* entry = FindBody(body);
            if (entry == nullptr)
            {
                return pose;
            }
            const b2Transform transform = b2Body_GetTransform(entry->body);
            pose.position = {transform.p.x, transform.p.y};
            pose.rotationDegrees = FromRot(transform.q);
            pose.scale = entry->scale;
            return pose;
        }

        void MoveBodyTo(PhysicsBodyHandle2D body, const Pose2D& pose, float stepSeconds) override
        {
            const BodyEntry* entry = FindBody(body);
            if (entry == nullptr || stepSeconds <= 0.0f)
            {
                return;
            }
            // Box2D が「今の場所から target まで、stepSeconds 秒で着く速度」を計算して体に付ける
            const b2Transform target{b2Vec2{pose.position.x, pose.position.y}, ToRot(pose.rotationDegrees)};
            b2Body_SetTargetTransform(entry->body, target, stepSeconds);
        }

        void SetLinearVelocity(PhysicsBodyHandle2D body, const core::Vector2& velocity) override
        {
            if (const BodyEntry* entry = FindBody(body))
            {
                b2Body_SetLinearVelocity(entry->body, b2Vec2{velocity.x, velocity.y});
            }
        }

        core::Vector2 GetLinearVelocity(PhysicsBodyHandle2D body) const override
        {
            const BodyEntry* entry = FindBody(body);
            if (entry == nullptr)
            {
                return {};
            }
            const b2Vec2 velocity = b2Body_GetLinearVelocity(entry->body);
            return {velocity.x, velocity.y};
        }

        void SetAngularVelocity(PhysicsBodyHandle2D body, float degreesPerSecond) override
        {
            if (const BodyEntry* entry = FindBody(body))
            {
                b2Body_SetAngularVelocity(entry->body, degreesPerSecond * Pi / 180.0f); // Box2D はラジアン/秒
            }
        }

        float GetAngularVelocity(PhysicsBodyHandle2D body) const override
        {
            const BodyEntry* entry = FindBody(body);
            return entry != nullptr ? b2Body_GetAngularVelocity(entry->body) * 180.0f / Pi : 0.0f;
        }

        void ApplyForce(PhysicsBodyHandle2D body, const core::Vector2& force) override
        {
            if (const BodyEntry* entry = FindBody(body))
            {
                b2Body_ApplyForceToCenter(entry->body, b2Vec2{force.x, force.y}, true); // true: 眠っていたら起こす
            }
        }

        void ApplyImpulse(PhysicsBodyHandle2D body, const core::Vector2& impulse) override
        {
            if (const BodyEntry* entry = FindBody(body))
            {
                b2Body_ApplyLinearImpulseToCenter(entry->body, b2Vec2{impulse.x, impulse.y}, true);
            }
        }

        float GetMass(PhysicsBodyHandle2D body) const override
        {
            const BodyEntry* entry = FindBody(body);
            return entry != nullptr ? b2Body_GetMass(entry->body) : 0.0f;
        }

        void SetGravity(const core::Vector2& gravity) override
        {
            b2World_SetGravity(m_world, b2Vec2{gravity.x, gravity.y});
        }

        core::Vector2 GetGravity() const override
        {
            const b2Vec2 gravity = b2World_GetGravity(m_world);
            return {gravity.x, gravity.y};
        }

        std::size_t GetShapeCount() const override
        {
            return m_entries.size();
        }

        void Step(float dt) override
        {
            if (dt > 0.0f)
            {
                b2World_Step(m_world, dt, 4); // 4: 1 歩の中を細かく分ける回数(Box2D の推奨値)
                CollectContactEvents();
            }
        }

        void TakeContactEvents(std::vector<ContactEvent2D>& out) override
        {
            out.clear();
            out.swap(m_contactEvents); // swap: 中身を入れ替える(コピーしない。空になった out の入れ物は世界が使い回す)
        }

        // Box2D の接触のイベントは、次の Step で消える。1 歩ごとに自前の形に写して貯める。
        void CollectContactEvents()
        {
            const b2ContactEvents events = b2World_GetContactEvents(m_world);
            for (int i = 0; i < events.beginCount; ++i)
            {
                const b2ContactBeginTouchEvent& begin = events.beginEvents[i];
                ContactEvent2D event;
                event.begin = true;
                event.ownerA = OwnerOf(b2StoreShapeId(begin.shapeIdA));
                event.ownerB = OwnerOf(b2StoreShapeId(begin.shapeIdB));
                event.normal = {begin.manifold.normal.x, begin.manifold.normal.y}; // Box2D の向きも A → B
                if (begin.manifold.pointCount > 0)
                {
                    event.point = {begin.manifold.points[0].point.x, begin.manifold.points[0].point.y};
                }
                m_contactEvents.push_back(event);
            }
            for (int i = 0; i < events.endCount; ++i)
            {
                const b2ContactEndTouchEvent& end = events.endEvents[i];
                ContactEvent2D event;
                event.begin = false;
                // 形がもう消えていることがある(そのときは控えにも無いので、持ち主は null になる)
                event.ownerA = OwnerOf(b2StoreShapeId(end.shapeIdA));
                event.ownerB = OwnerOf(b2StoreShapeId(end.shapeIdB));
                m_contactEvents.push_back(event);
            }
        }

        void OverlapPoint(float x, float y, std::uint32_t mask, std::vector<QueryHit2D>& out) const override
        {
            // 点のまわりの小さな範囲で候補を探し(速い)、本当に点が形の中かを1つずつ調べる(正確)
            const b2Vec2 point{x, y};
            const float margin = 0.001f;
            m_found.clear();
            QueryContext context{&m_found, &point};
            b2World_OverlapAABB(m_world, b2AABB{{x - margin, y - margin}, {x + margin, y + margin}}, ToQueryFilter(mask),
                                CollectShape, &context);
            ToHits(out);
        }

        void OverlapShape(const ShapeDesc2D& desc, const Pose2D& pose, std::uint32_t mask,
                          std::vector<QueryHit2D>& out) const override
        {
            const b2ShapeProxy proxy = MakeWorldProxy(desc, pose);
            m_found.clear();
            QueryContext context{&m_found, nullptr};
            b2World_OverlapShape(m_world, &proxy, ToQueryFilter(mask), CollectShape, &context);
            ToHits(out);
        }

        bool Raycast(float fromX, float fromY, float toX, float toY, std::uint32_t mask, RaycastHit2D& out) const override
        {
            // Box2D は「始点」と「進む量(終点 - 始点)」で受け取る
            const b2RayResult result =
                b2World_CastRayClosest(m_world, b2Vec2{fromX, fromY}, b2Vec2{toX - fromX, toY - fromY}, ToQueryFilter(mask));
            if (!result.hit)
            {
                return false;
            }
            out.shape.id = b2StoreShapeId(result.shapeId);
            out.owner = OwnerOf(out.shape.id);
            out.point = {result.point.x, result.point.y};
            out.normal = {result.normal.x, result.normal.y};
            out.fraction = result.fraction;
            return true;
        }

    private:
        // 置いた形 1 つの控え(Box2D の外の情報: 決め事、持ち主、今の拡大)。
        struct ShapeEntry
        {
            ShapeDesc2D desc;
            b2BodyId body;
            core::Vector2 scale;
            bool ownsBody = true; // true: AddShape が作った自分だけの体(形を消すと体も消す)。false: AttachShape で体に付けた形
        };

        // AddBody で置いた体 1 つの控え。
        struct BodyEntry
        {
            b2BodyId body;
            core::Vector2 scale;               // 付けた形の大きさに効く拡大縮小
            std::vector<std::uint64_t> shapes; // 付いている形(拡大縮小が変わったら作り直す、体を消したら控えも消す)
        };

        BodyEntry* FindBody(PhysicsBodyHandle2D body)
        {
            const auto found = m_bodies.find(body.id);
            return found != m_bodies.end() ? &found->second : nullptr;
        }
        const BodyEntry* FindBody(PhysicsBodyHandle2D body) const
        {
            const auto found = m_bodies.find(body.id);
            return found != m_bodies.end() ? &found->second : nullptr;
        }

        // desc の形を、拡大した大きさで体に作る。
        static b2ShapeId CreateScaledShape(b2BodyId body, const ShapeDesc2D& desc, const core::Vector2& scale)
        {
            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.filter = ToFilter(desc.filter);
            shapeDef.density = desc.density;
            shapeDef.material.friction = desc.friction;
            shapeDef.material.restitution = desc.restitution;
            shapeDef.isSensor = desc.isTrigger;    // トリガーはセンサー(押し合わない。重なりは自前の問い合わせで調べる)
            shapeDef.enableContactEvents = true;   // 触れ始めた・離れたイベントを出す(PHY-3)
            if (desc.type == ShapeType2D::Circle)
            {
                const b2Circle circle = MakeScaledCircle(desc, scale);
                return b2CreateCircleShape(body, &shapeDef, &circle);
            }
            const b2Polygon box = MakeScaledBox(desc, scale);
            return b2CreatePolygonShape(body, &shapeDef, &box);
        }

        // 形を、新しい拡大縮小の大きさに作り直す(Box2D の形は拡大縮小を持たないため)。
        static void RescaleShape(b2ShapeId shape, const ShapeDesc2D& desc, const core::Vector2& scale)
        {
            if (desc.type == ShapeType2D::Circle)
            {
                const b2Circle circle = MakeScaledCircle(desc, scale);
                b2Shape_SetCircle(shape, &circle);
            }
            else
            {
                const b2Polygon box = MakeScaledBox(desc, scale);
                b2Shape_SetPolygon(shape, &box);
            }
        }

        core::ObjectHandle OwnerOf(std::uint64_t id) const
        {
            const auto found = m_entries.find(id);
            return found != m_entries.end() ? found->second.desc.owner : core::ObjectHandle();
        }

        // 見つけた Box2D の形(m_found)を、外へ返す結果にする。
        void ToHits(std::vector<QueryHit2D>& out) const
        {
            out.clear();
            for (const b2ShapeId shapeId : m_found)
            {
                QueryHit2D hit;
                hit.shape.id = b2StoreShapeId(shapeId);
                hit.owner = OwnerOf(hit.shape.id);
                out.push_back(hit);
            }
        }

        b2WorldId m_world;
        std::unordered_map<std::uint64_t, ShapeEntry> m_entries; // 形のハンドル(id)→ 控え
        std::unordered_map<std::uint64_t, BodyEntry> m_bodies;   // 体のハンドル(id)→ 控え(AddBody で置いたものだけ)
        // 問い合わせの途中で使う入れ物(使い回して、毎回の確保を避ける)。問い合わせは const だが、中身を書き換えるので mutable
        mutable std::vector<b2ShapeId> m_found;
        // 進めている間に貯まった接触のイベント(TakeContactEvents で外へ渡す)
        std::vector<ContactEvent2D> m_contactEvents;
    };

    std::unique_ptr<PhysicsWorld2D> CreatePhysicsWorld2D()
    {
        // 中身を替えるときは、ここで作るものを替える(使う側は PhysicsWorld2D として扱うので変わらない)
        return std::make_unique<Box2DPhysicsWorld2D>();
    }
}
