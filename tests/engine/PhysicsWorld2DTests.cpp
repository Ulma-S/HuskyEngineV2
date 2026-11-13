// PhysicsWorld2D(2D の当たり判定のインターフェース)のテスト(COL-2)。
// 自前の型だけを使い、Box2D は知らない(中身を替えても、このテストはそのまま通るはず)。
#include "engine/PhysicsWorld2D.h"

#include "core/ObjectRegistry.h"
#include "engine/PhysicsMemory.h"

#include <doctest/doctest.h>

#include <vector>

using husky::engine::AllCollisionLayers;
using husky::engine::CreatePhysicsWorld2D;
using husky::engine::PhysicsShapeHandle2D;
using husky::engine::Pose2D;
using husky::engine::QueryHit2D;
using husky::engine::RaycastHit2D;
using husky::engine::ShapeDesc2D;
using husky::engine::ShapeType2D;

namespace
{
    Pose2D PoseAt(float x, float y, float rotation = 0.0f, float scaleX = 1.0f, float scaleY = 1.0f)
    {
        Pose2D pose;
        pose.position = {x, y};
        pose.rotationDegrees = rotation;
        pose.scale = {scaleX, scaleY};
        return pose;
    }

    ShapeDesc2D Box(float width, float height)
    {
        ShapeDesc2D desc;
        desc.type = ShapeType2D::Box;
        desc.width = width;
        desc.height = height;
        return desc;
    }

    ShapeDesc2D Circle(float radius)
    {
        ShapeDesc2D desc;
        desc.type = ShapeType2D::Circle;
        desc.radius = radius;
        return desc;
    }

    // 点 (x, y) に重なる形の数
    std::size_t CountAt(const husky::engine::PhysicsWorld2D& world, float x, float y, std::uint32_t mask = AllCollisionLayers)
    {
        std::vector<QueryHit2D> hits;
        world.OverlapPoint(x, y, mask, hits);
        return hits.size();
    }
}

TEST_CASE("COL-2: 四角を置くと、点の問い合わせで見つかる。回転・拡大縮小に合わせて形が変わる")
{
    auto world = CreatePhysicsWorld2D();
    ShapeDesc2D desc = Box(40.0f, 20.0f);
    desc.owner = husky::core::ObjectHandle(); // 持ち主なし(結果の owner も空)
    const PhysicsShapeHandle2D shape = world->AddShape(desc, PoseAt(100.0f, 50.0f));
    REQUIRE_FALSE(shape.IsNull());
    CHECK(world->GetShapeCount() == 1);

    // 真ん中 (100, 50)、幅 40、高さ 20 → x は 80〜120、y は 40〜60
    std::vector<QueryHit2D> hits;
    world->OverlapPoint(110.0f, 55.0f, AllCollisionLayers, hits);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].shape == shape);
    CHECK(CountAt(*world, 125.0f, 50.0f) == 0);
    CHECK(CountAt(*world, 100.0f, 65.0f) == 0);

    // 90 度回すと縦長(幅 20、高さ 40)
    world->SetPose(shape, PoseAt(100.0f, 50.0f, 90.0f));
    CHECK(CountAt(*world, 100.0f, 65.0f) == 1);
    CHECK(CountAt(*world, 115.0f, 50.0f) == 0);

    // 回さずに x を 2 倍: 幅 80 → x は 60〜140
    world->SetPose(shape, PoseAt(100.0f, 50.0f, 0.0f, 2.0f, 1.0f));
    CHECK(CountAt(*world, 135.0f, 50.0f) == 1);
    CHECK(CountAt(*world, 145.0f, 50.0f) == 0);

    // 消すと見つからない
    world->RemoveShape(shape);
    CHECK(world->GetShapeCount() == 0);
    CHECK(CountAt(*world, 100.0f, 50.0f) == 0);
    world->RemoveShape(shape); // 2 回消しても何も起きない
}

TEST_CASE("COL-2: 円は offset で持ち主からずらせ、縦横で違う拡大は大きいほうに合わせる")
{
    auto world = CreatePhysicsWorld2D();
    ShapeDesc2D desc = Circle(10.0f);
    desc.offset = {5.0f, 0.0f};
    const PhysicsShapeHandle2D shape = world->AddShape(desc, PoseAt(0.0f, 0.0f));
    // 真ん中は (5, 0)、半径 10 → x は -5〜15
    CHECK(CountAt(*world, 14.0f, 0.0f) == 1);
    CHECK(CountAt(*world, -6.0f, 0.0f) == 0);
    // 拡大 (1, 3): 半径は 3 倍の 30、offset は x に 1 倍で (5, 0) のまま
    world->SetPose(shape, PoseAt(0.0f, 0.0f, 0.0f, 1.0f, 3.0f));
    CHECK(CountAt(*world, 5.0f, 29.0f) == 1); // 真ん中 (5, 0) から 29 離れている(半径 30 の内側)
    CHECK(CountAt(*world, 5.0f, 31.0f) == 0); // 31 離れている(外側)
}

TEST_CASE("COL-2: レイヤーとマスク: 問い合わせのマスクに当てはまるレイヤーの形だけが見つかる")
{
    auto world = CreatePhysicsWorld2D();
    ShapeDesc2D enemy = Box(10.0f, 10.0f);
    enemy.filter.layer = 1u << 2; // グループ 2
    world->AddShape(enemy, PoseAt(0.0f, 0.0f));
    CHECK(CountAt(*world, 0.0f, 0.0f, 1u << 0) == 0);           // グループ 0 だけを探す
    CHECK(CountAt(*world, 0.0f, 0.0f, 1u << 2) == 1);           // グループ 2 を探す
    CHECK(CountAt(*world, 0.0f, 0.0f, (1u << 0) | (1u << 2)) == 1); // どちらか
}

TEST_CASE("COL-2: 形の問い合わせ(円と四角の重なり)と、持ち主のハンドルが返ること")
{
    husky::core::ObjectRegistry registry;
    auto* owner = registry.Create<husky::core::Object>();
    REQUIRE(owner != nullptr);
    auto world = CreatePhysicsWorld2D();
    ShapeDesc2D box = Box(40.0f, 40.0f);
    box.owner = owner->GetHandle();
    world->AddShape(box, PoseAt(0.0f, 0.0f)); // x、y とも -20〜20

    std::vector<QueryHit2D> hits;
    world->OverlapShape(Circle(10.0f), PoseAt(25.0f, 0.0f), AllCollisionLayers, hits); // 円は x が 15〜35 → 重なる
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner == owner->GetHandle());
    world->OverlapShape(Circle(10.0f), PoseAt(35.0f, 0.0f), AllCollisionLayers, hits); // 25〜45 → 重ならない
    CHECK(hits.empty());
    // 45 度回した四角(角が 28 まで出る)なら、x が 25 の点の近くに届く
    world->OverlapShape(Box(10.0f, 10.0f), PoseAt(27.0f, 0.0f, 45.0f), AllCollisionLayers, hits);
    CHECK(hits.size() == 1);
}

TEST_CASE("COL-2: レイはいちばん手前の形に当たり、当たった点と面の向きを返す")
{
    auto world = CreatePhysicsWorld2D();
    const PhysicsShapeHandle2D nearBox = world->AddShape(Box(40.0f, 20.0f), PoseAt(100.0f, 50.0f)); // 左の辺は x = 80
    world->AddShape(Box(40.0f, 20.0f), PoseAt(200.0f, 50.0f));
    RaycastHit2D hit;
    REQUIRE(world->Raycast(0.0f, 50.0f, 300.0f, 50.0f, AllCollisionLayers, hit));
    CHECK(hit.shape == nearBox);
    CHECK(hit.point.x == doctest::Approx(80.0f).epsilon(0.01));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
    CHECK(hit.fraction == doctest::Approx(80.0f / 300.0f).epsilon(0.01));
    // 上を通るレイは当たらない
    CHECK_FALSE(world->Raycast(0.0f, 100.0f, 300.0f, 100.0f, AllCollisionLayers, hit));
}

TEST_CASE("COL-2: 世界を消すと、中身の確保は全部返る")
{
    {
        auto world = CreatePhysicsWorld2D();
        world->AddShape(Box(10.0f, 10.0f), PoseAt(0.0f, 0.0f));
        world->Step(1.0f / 60.0f);
        CHECK(husky::engine::GetPhysicsAllocatedBytes() > 0);
    } // ここで world が消える(unique_ptr)
    CHECK(husky::engine::GetPhysicsAllocatedBytes() == 0);
}

TEST_CASE("宿題: 拡大縮小 0 や大きさ 0 の形でも止まらず、どの点にも当たらない(大きさ 0 の形は、とても小さな形として扱う)")
{
    auto world = CreatePhysicsWorld2D();
    // 拡大 0 の四角と円、大きさ 0 の四角、半径 0 の円
    const PhysicsShapeHandle2D zeroScaleBox = world->AddShape(Box(40.0f, 20.0f), PoseAt(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    world->AddShape(Circle(10.0f), PoseAt(100.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    world->AddShape(Box(0.0f, 0.0f), PoseAt(200.0f, 0.0f));
    world->AddShape(Circle(0.0f), PoseAt(300.0f, 0.0f));
    world->Step(1.0f / 60.0f);
    CHECK(world->GetShapeCount() == 4);
    CHECK(CountAt(*world, 5.0f, 0.0f) == 0);
    CHECK(CountAt(*world, 105.0f, 0.0f) == 0);
    CHECK(CountAt(*world, 205.0f, 0.0f) == 0);
    CHECK(CountAt(*world, 305.0f, 0.0f) == 0);
    // 拡大 0 から元に戻すと、ふつうに当たる(作り直しても壊れない)
    world->SetPose(zeroScaleBox, PoseAt(0.0f, 0.0f));
    CHECK(CountAt(*world, 15.0f, 5.0f) == 1);
    // 大きさがマイナスでも、絶対値の大きさとして扱う
    world->AddShape(Box(-40.0f, -20.0f), PoseAt(400.0f, 0.0f));
    CHECK(CountAt(*world, 415.0f, 5.0f) == 1);
}

TEST_CASE("宿題: マイナスの拡大(反転)では、大きさは絶対値、ずらし(offset)は向きごと反転する")
{
    auto world = CreatePhysicsWorld2D();
    // 四角(幅 20)を、持ち主から右へ 30 ずらす。x を -1 倍(左右反転)すると、左へ 30 の所に来る
    ShapeDesc2D box = Box(20.0f, 20.0f);
    box.offset = {30.0f, 0.0f};
    world->AddShape(box, PoseAt(0.0f, 0.0f, 0.0f, -1.0f, 1.0f));
    CHECK(CountAt(*world, -30.0f, 0.0f) == 1);
    CHECK(CountAt(*world, 30.0f, 0.0f) == 0);
    // 円(半径 10)を上へ 20 ずらし、(1, -2) 倍: 半径は 20(大きいほうの絶対値)、真ん中は下へ 40
    ShapeDesc2D circle = Circle(10.0f);
    circle.offset = {0.0f, 20.0f};
    world->AddShape(circle, PoseAt(100.0f, 0.0f, 0.0f, 1.0f, -2.0f));
    CHECK(CountAt(*world, 100.0f, -40.0f) == 1);
    CHECK(CountAt(*world, 100.0f, -58.0f) == 1); // 真ん中から 18(半径 20 の内側)
    CHECK(CountAt(*world, 100.0f, 40.0f) == 0);
}
