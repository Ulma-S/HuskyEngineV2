// CameraFollowComponent(相手を追いかける)のテスト。
#include "game/CameraFollowComponent.h"

#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/World.h"

#include <doctest/doctest.h>

#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::game::CameraFollowComponent;
using husky::platform::KeyEvent;

namespace
{
    const std::vector<KeyEvent> NoEvents;

    // 毎フレーム、秒速 100 で右へ動く(アクター自身の Tick は TickOrder::Default)。
    class FollowTestRunner : public Actor
    {
        HUSKY_OBJECT_INLINE(FollowTestRunner, Actor)
    public:
        void Tick(double dt) override { transform.position.x += static_cast<float>(100.0 * dt); }
    };
}

TEST_CASE("CameraFollow: 相手が動いた同じフレームのうちに、相手の位置 + offset へ動く(z は動かさない)")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    FollowTestRunner* runner = app.GetWorld().Spawn<FollowTestRunner>();
    Actor* camera = app.GetWorld().Spawn<Actor>();
    REQUIRE(runner != nullptr);
    REQUIRE(camera != nullptr);
    runner->transform.position = {10.0f, 20.0f, 0.0f};
    camera->transform.position.z = 5.0f;
    CameraFollowComponent* follow = camera->AddComponent<CameraFollowComponent>();
    REQUIRE(follow != nullptr);
    follow->target = runner->GetHandle();
    follow->offset = {1.0f, 2.0f, 3.0f};

    app.StepFrame(0, NoEvents);
    // 相手は 10 → 20 へ動いた。カメラは「動いた後」の位置を追う(TickOrder::Late なので、1 フレーム遅れない)
    CHECK(runner->transform.position.x == doctest::Approx(20.0f));
    CHECK(camera->transform.position.x == doctest::Approx(21.0f));
    CHECK(camera->transform.position.y == doctest::Approx(22.0f));
    CHECK(camera->transform.position.z == 5.0f);
}

TEST_CASE("CameraFollow: 相手がいない(何も指さない、Destroy された)ときは、その場に止まる")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Actor* camera = app.GetWorld().Spawn<Actor>();
    FollowTestRunner* runner = app.GetWorld().Spawn<FollowTestRunner>();
    REQUIRE(camera != nullptr);
    REQUIRE(runner != nullptr);
    camera->transform.position = {7.0f, 8.0f, 0.0f};
    CameraFollowComponent* follow = camera->AddComponent<CameraFollowComponent>();
    REQUIRE(follow != nullptr);

    app.StepFrame(0, NoEvents); // 何も指さない
    CHECK(camera->transform.position.x == 7.0f);

    follow->target = runner->GetHandle();
    runner->Destroy();
    app.StepFrame(0, NoEvents); // Destroy された相手は追わない
    CHECK(camera->transform.position.x == 7.0f);
    app.StepFrame(0, NoEvents); // 消えた後(ハンドルが古くなった後)も、止まったまま
    CHECK(camera->transform.position.x == 7.0f);
}
