// PhysicsWorld2D の中身: Box2D(COL-2)。Box2D の型と関数を使うのは、このファイル(と PhysicsMemory.cpp)だけ。
// ここを別の中身(自作、ほかのライブラリ)に替えても、PhysicsWorld2D.h を使う側は変わらない(設計メモ「当たり判定と物理」)。
//
// Box2D の考え方(Unity の Rigidbody2D と Collider2D にあたる):
//   - 世界(b2World)の中に、体(b2Body。位置と回転を持つ)を置き、体に形(b2Shape。四角や円)を付ける。
//   - 体には種類がある: 動かない(static)、コードが動かす(kinematic)、物理で動く(dynamic)。
//     今は当たり判定だけなので、どの形も「コードが動かす体」に1つずつ付ける(kinematic 同士は押し合わない)。
//     物理(Rigidbody2D)を足すときに、dynamic の体と、1つの体に複数の形を付ける形にする。
//   - ID(b2BodyId、b2ShapeId)で指す。世代付きなので、消した後の古い ID は無効と分かる(エンジンのハンドルと同じ考え方)。
//
// 拡大縮小: Box2D の形は拡大縮小を持たないので、拡大した大きさで形を作り直す(SetPose で拡大が変わったときだけ)。
// 動作スレッド: ゲームスレッドのみ
#include "engine/PhysicsWorld2D.h"

#include "engine/PhysicsMemory.h"

#include <box2d/box2d.h>

#include <algorithm>     // std::max
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
            worldDef.gravity = b2Vec2{0.0f, 0.0f};     // 今は当たり判定だけ(物理を足すときに重力を決める)
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
            bodyDef.type = b2_kinematicBody; // コードが動かす体(今は当たり判定だけ)
            bodyDef.position = b2Vec2{pose.position.x, pose.position.y};
            bodyDef.rotation = ToRot(pose.rotationDegrees);
            const b2BodyId body = b2CreateBody(m_world, &bodyDef);

            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.filter = ToFilter(desc.filter);
            b2ShapeId shape;
            if (desc.type == ShapeType2D::Circle)
            {
                const b2Circle circle = MakeScaledCircle(desc, pose.scale);
                shape = b2CreateCircleShape(body, &shapeDef, &circle);
            }
            else
            {
                const b2Polygon box = MakeScaledBox(desc, pose.scale);
                shape = b2CreatePolygonShape(body, &shapeDef, &box);
            }

            // Box2D の形の ID を、整数 1 つにしまう(b2StoreShapeId。0 にはならない)。これを外へのハンドルにする
            PhysicsShapeHandle2D handle;
            handle.id = b2StoreShapeId(shape);
            m_entries[handle.id] = Entry{desc, body, pose.scale};
            return handle;
        }

        void SetPose(PhysicsShapeHandle2D shape, const Pose2D& pose) override
        {
            const auto found = m_entries.find(shape.id);
            if (found == m_entries.end())
            {
                return;
            }
            Entry& entry = found->second;
            b2Body_SetTransform(entry.body, b2Vec2{pose.position.x, pose.position.y}, ToRot(pose.rotationDegrees));
            if (pose.scale.x != entry.scale.x || pose.scale.y != entry.scale.y)
            {
                // 拡大縮小が変わった: 形を作り直す(Box2D の形は拡大縮小を持たないため)
                const b2ShapeId shapeId = b2LoadShapeId(shape.id);
                if (entry.desc.type == ShapeType2D::Circle)
                {
                    const b2Circle circle = MakeScaledCircle(entry.desc, pose.scale);
                    b2Shape_SetCircle(shapeId, &circle);
                }
                else
                {
                    const b2Polygon box = MakeScaledBox(entry.desc, pose.scale);
                    b2Shape_SetPolygon(shapeId, &box);
                }
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
            b2DestroyBody(found->second.body); // 体を消すと、付いている形も消える
            m_entries.erase(found);
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
        struct Entry
        {
            ShapeDesc2D desc;
            b2BodyId body;
            core::Vector2 scale;
        };

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
        std::unordered_map<std::uint64_t, Entry> m_entries; // 形のハンドル(id)→ 控え
        // 問い合わせの途中で使う入れ物(使い回して、毎回の確保を避ける)。問い合わせは const だが、中身を書き換えるので mutable
        mutable std::vector<b2ShapeId> m_found;
    };

    std::unique_ptr<PhysicsWorld2D> CreatePhysicsWorld2D()
    {
        // 中身を替えるときは、ここで作るものを替える(使う側は PhysicsWorld2D として扱うので変わらない)
        return std::make_unique<Box2DPhysicsWorld2D>();
    }
}
