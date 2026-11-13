// PhysicsWorld2D の体(物理で動くもの)のテスト(PHY-1)。自前の型だけを使い、Box2D は知らない。
// 物理は決まった刻み(1/60 秒)で進め、毎回同じ結果になることを前提に、だいたいの値で確かめる。
#include "engine/PhysicsWorld2D.h"

#include <doctest/doctest.h>

#include <cmath> // std::abs
#include <memory>
#include <vector>

using husky::engine::AllCollisionLayers;
using husky::engine::BodyDesc2D;
using husky::engine::BodyType2D;
using husky::engine::CreatePhysicsWorld2D;
using husky::engine::PhysicsBodyHandle2D;
using husky::engine::PhysicsShapeHandle2D;
using husky::engine::PhysicsWorld2D;
using husky::engine::Pose2D;
using husky::engine::QueryHit2D;
using husky::engine::ShapeDesc2D;
using husky::engine::ShapeType2D;

namespace
{
    constexpr float Step = 1.0f / 60.0f;

    Pose2D PoseAt(float x, float y, float scaleX = 1.0f, float scaleY = 1.0f)
    {
        Pose2D pose;
        pose.position = {x, y};
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

    BodyDesc2D BodyOf(BodyType2D type)
    {
        BodyDesc2D desc;
        desc.type = type;
        return desc;
    }

    void StepSeconds(PhysicsWorld2D& world, float seconds)
    {
        const int steps = static_cast<int>(seconds / Step + 0.5f);
        for (int i = 0; i < steps; ++i)
        {
            world.Step(Step);
        }
    }
}

TEST_CASE("PHY-1: 重力の既定は 0。重力を入れると Dynamic の体は落ち、Static の床の上で止まる")
{
    std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
    CHECK(world->GetGravity().y == 0.0f);
    world->SetGravity({0.0f, -980.0f}); // 9.8 メートル/秒²(1 メートル = 100 単位)

    const PhysicsBodyHandle2D floor = world->AddBody(BodyOf(BodyType2D::Static), PoseAt(0.0f, 0.0f));
    world->AttachShape(floor, Box(1000.0f, 20.0f)); // 上の面は y = 10
    const PhysicsBodyHandle2D box = world->AddBody(BodyOf(BodyType2D::Dynamic), PoseAt(0.0f, 200.0f));
    REQUIRE_FALSE(world->AttachShape(box, Box(32.0f, 32.0f)).IsNull());
    CHECK(world->GetBodyCount() == 2);
    CHECK(world->GetShapeCount() == 2);

    StepSeconds(*world, 0.25f);
    CHECK(world->GetBodyPose(box).position.y < 200.0f); // 落ちている
    CHECK(world->GetLinearVelocity(box).y < 0.0f);

    StepSeconds(*world, 3.0f);
    CHECK(world->GetBodyPose(box).position.y == doctest::Approx(26.0f).epsilon(0.05)); // 床の上(10 + 半分の 16)
    CHECK(std::abs(world->GetLinearVelocity(box).y) < 1.0f);                         // 止まっている
    CHECK(world->GetBodyPose(floor).position.y == 0.0f);                              // 床は動かない
}

TEST_CASE("PHY-1: 自由落下は 1 秒でおよそ 1/2 × g × t² 落ちる。重力の倍率 0 なら落ちず、速度のとおりに進む")
{
    std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
    world->SetGravity({0.0f, -980.0f});
    const PhysicsBodyHandle2D falling = world->AddBody(BodyOf(BodyType2D::Dynamic), PoseAt(0.0f, 0.0f));
    world->AttachShape(falling, Box(10.0f, 10.0f));
    BodyDesc2D floating = BodyOf(BodyType2D::Dynamic);
    floating.gravityScale = 0.0f;
    const PhysicsBodyHandle2D moving = world->AddBody(floating, PoseAt(1000.0f, 0.0f));
    world->AttachShape(moving, Box(10.0f, 10.0f));
    world->SetLinearVelocity(moving, {120.0f, 0.0f});

    StepSeconds(*world, 1.0f);
    // 1/2 × 980 × 1² = 490。1 歩ずつ速度を足してから進める計算なので、少し多めになる(約 498)
    CHECK(world->GetBodyPose(falling).position.y < -480.0f);
    CHECK(world->GetBodyPose(falling).position.y > -510.0f);
    CHECK(world->GetLinearVelocity(falling).y == doctest::Approx(-980.0f).epsilon(0.01));
    CHECK(world->GetBodyPose(moving).position.x == doctest::Approx(1120.0f).epsilon(0.001));
    CHECK(world->GetBodyPose(moving).position.y == doctest::Approx(0.0f));
}

TEST_CASE("PHY-1: 重さは 面積 × 密度。衝撃は 速さの変化 = 衝撃 / 重さ。回る速さは度で読み書きする")
{
    std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
    const PhysicsBodyHandle2D body = world->AddBody(BodyOf(BodyType2D::Dynamic), PoseAt(0.0f, 0.0f));
    ShapeDesc2D heavy = Box(32.0f, 32.0f);
    heavy.density = 2.0f;
    world->AttachShape(body, heavy);
    CHECK(world->GetMass(body) == doctest::Approx(2048.0f)); // 32 × 32 × 2

    world->ApplyImpulse(body, {2048.0f * 50.0f, 0.0f});
    CHECK(world->GetLinearVelocity(body).x == doctest::Approx(50.0f));

    world->SetAngularVelocity(body, 90.0f);
    CHECK(world->GetAngularVelocity(body) == doctest::Approx(90.0f));
    world->SetLinearVelocity(body, {0.0f, 0.0f});
    StepSeconds(*world, 1.0f);
    CHECK(world->GetBodyPose(body).rotationDegrees == doctest::Approx(90.0f).epsilon(0.01));

    const PhysicsBodyHandle2D still = world->AddBody(BodyOf(BodyType2D::Static), PoseAt(0.0f, 0.0f));
    world->AttachShape(still, Box(32.0f, 32.0f));
    CHECK(world->GetMass(still) == 0.0f); // 動かない体に重さは無い
}

TEST_CASE("PHY-1: Kinematic の体を MoveBodyTo で動かすと、通り道の Dynamic の体を押しのける")
{
    std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
    const PhysicsBodyHandle2D pusher = world->AddBody(BodyOf(BodyType2D::Kinematic), PoseAt(0.0f, 0.0f));
    world->AttachShape(pusher, Box(32.0f, 32.0f));
    const PhysicsBodyHandle2D crate = world->AddBody(BodyOf(BodyType2D::Dynamic), PoseAt(40.0f, 0.0f)); // すき間 8
    world->AttachShape(crate, Box(32.0f, 32.0f));

    float x = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        x += 2.0f; // 1 秒で 120 進める
        world->MoveBodyTo(pusher, PoseAt(x, 0.0f), Step);
        world->Step(Step);
    }
    CHECK(world->GetBodyPose(pusher).position.x == doctest::Approx(120.0f).epsilon(0.01)); // 決めた場所に着いている
    CHECK(world->GetBodyPose(crate).position.x > 140.0f); // 押されて、押す側より前にいる(120 + 32 くらい)
}

TEST_CASE("PHY-1: 体の瞬間移動と拡大縮小、形だけ外す、体ごと消す、知らない体")
{
    std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
    const PhysicsBodyHandle2D body = world->AddBody(BodyOf(BodyType2D::Dynamic), PoseAt(0.0f, 0.0f));
    const PhysicsShapeHandle2D a = world->AttachShape(body, Box(10.0f, 10.0f));
    ShapeDesc2D right = Box(10.0f, 10.0f);
    right.offset = {20.0f, 0.0f};
    const PhysicsShapeHandle2D b = world->AttachShape(body, right);
    CHECK(world->GetMass(body) == doctest::Approx(200.0f));

    // 瞬間移動 + 2 倍: 付いている形は一緒に動き、ずらしも大きさも 2 倍になる(右の形の中心は 500 + 40)
    world->SetBodyPose(body, PoseAt(500.0f, 0.0f, 2.0f, 2.0f));
    std::vector<QueryHit2D> hits;
    world->OverlapPoint(540.0f, 9.0f, AllCollisionLayers, hits);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].shape == b);
    CHECK(world->GetBodyPose(body).scale.x == 2.0f);

    // 体に付けた形は SetPose では動かない(体と一緒に動く)
    world->SetPose(a, PoseAt(-500.0f, 0.0f));
    world->OverlapPoint(500.0f, 0.0f, AllCollisionLayers, hits);
    CHECK(hits.size() == 1);

    // 形だけを外すと、体は残り、重さが減る
    world->RemoveShape(b);
    CHECK(world->GetShapeCount() == 1);
    CHECK(world->GetBodyCount() == 1);
    CHECK(world->GetMass(body) == doctest::Approx(400.0f)); // 残った形は 20 × 20

    // 体ごと消すと、付いている形も消える
    world->RemoveBody(body);
    CHECK(world->GetBodyCount() == 0);
    CHECK(world->GetShapeCount() == 0);
    world->OverlapPoint(500.0f, 0.0f, AllCollisionLayers, hits);
    CHECK(hits.empty());

    // 知らない体: 何もしない・既定の値
    CHECK(world->AttachShape(body, Box(10.0f, 10.0f)).IsNull());
    world->SetLinearVelocity(body, {1.0f, 0.0f});
    CHECK(world->GetMass(body) == 0.0f);
    CHECK(world->GetBodyPose(PhysicsBodyHandle2D{}).position.x == 0.0f);
}
