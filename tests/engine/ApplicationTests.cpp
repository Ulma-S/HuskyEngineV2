// Application のテスト。Run(ウィンドウと GPU)は使わず、StepFrame で1フレームずつ進める。
// dt は FrameClock::SetFixedDelta で固定するので、結果は毎回同じになる。
// FrameClock は dt を上限(既定 0.1 秒)で切り詰めるので、固定する dt はそれ以下にする。
#include "engine/Application.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::Scene;
using husky::platform::Key;
using husky::platform::KeyEvent;

namespace
{
    const std::vector<KeyEvent> NoEvents;

    // 毎フレーム、秒速 100 で右へ動く。Space が押されている間は、さらに秒速 100 で下へ動く。
    class Walker : public Actor
    {
        HUSKY_OBJECT(Walker, Actor)
    public:
        void Tick(double dt) override
        {
            x += static_cast<float>(100.0 * dt);
            const Scene* scene = GetScene();
            if (scene != nullptr && scene->GetInput()->IsDown(Key::Space))
            {
                y += static_cast<float>(100.0 * dt);
            }
        }
    };

    // Tick で "T" と書く。
    std::string g_log;
    class TickLogger : public Actor
    {
        HUSKY_OBJECT(TickLogger, Actor)
    public:
        void Tick(double) override { g_log += "T"; }
    };
}

TEST_CASE("Application: シーンは最初からあり、GC の根で、入力が渡されている")
{
    Application app;
    CHECK(app.GetRegistry().GetObjectCount() == 1); // シーンだけ
    CHECK(app.GetRegistry().GetRootCount() == 1);
    CHECK(app.GetScene().GetInput() == &app.GetInput());
}

TEST_CASE("Application: StepFrame は固定した dt でシーンを更新し、その dt を返す")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetScene().Spawn<Walker>();

    CHECK(app.StepFrame(0, NoEvents) == doctest::Approx(0.1));
    CHECK(app.StepFrame(0, NoEvents) == doctest::Approx(0.1));
    CHECK(walker->x == doctest::Approx(20.0)); // 100 × 0.1 × 2 フレーム
}

TEST_CASE("Application: StepFrame に渡したキーイベントが、アクターの読む入力になる")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetScene().Spawn<Walker>();

    app.StepFrame(0, {KeyEvent{Key::Space, true}}); // Space を押す
    CHECK(app.GetInput().WasPressed(Key::Space));
    CHECK(walker->y == doctest::Approx(10.0));

    app.StepFrame(0, NoEvents); // 押しっぱなし
    CHECK(walker->y == doctest::Approx(20.0));

    app.StepFrame(0, {KeyEvent{Key::Space, false}}); // 離す
    CHECK(walker->y == doctest::Approx(20.0));
}

TEST_CASE("Application: 更新コールバックは、シーンの更新の前に、同じ dt で呼ばれる")
{
    g_log.clear();
    Application app;
    app.GetClock().SetFixedDelta(0.05);
    app.GetScene().Spawn<TickLogger>();
    double receivedDt = 0.0;
    app.SetUpdateCallback([&](Application&, double dt) {
        g_log += "U";
        receivedDt = dt;
    });

    app.StepFrame(0, NoEvents);

    CHECK(g_log == "UT");
    CHECK(receivedDt == doctest::Approx(0.05));
}

TEST_CASE("Application: Destroy したアクターは、そのフレームの終わりに消える")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetScene().Spawn<Walker>();
    const auto handle = walker->GetHandle();

    walker->Destroy();
    app.StepFrame(0, NoEvents);

    CHECK(app.GetRegistry().Resolve(handle) == nullptr);
    CHECK(app.GetScene().actors.empty());
}

TEST_CASE("Application: GC は GcIntervalSeconds(1 秒)ごとに回り、シーンから届かないものを回収する")
{
    Application app;
    // 1/16 秒は2進数でちょうど表せるので、16 回足すとぴったり 1.0 になる。
    // (0.1 は2進数で表せず、10 回足しても 0.9999999... になって、GC が1フレーム遅れる)
    app.GetClock().SetFixedDelta(0.0625);
    app.GetRegistry().Create<Walker>(); // シーンにいない(根から届かない)アクター
    CHECK(app.GetRegistry().GetObjectCount() == 2);

    for (int i = 0; i < 15; ++i) // 0.9375 秒まで
    {
        app.StepFrame(0, NoEvents);
    }
    CHECK(app.GetRegistry().GetObjectCount() == 2); // まだ GC は回っていない

    app.StepFrame(0, NoEvents); // 1.0 秒 → GC
    CHECK(app.GetRegistry().GetObjectCount() == 1);
    CHECK(app.GetCollectedLastGc() == 1);
}
