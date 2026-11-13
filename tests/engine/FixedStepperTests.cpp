// 決まった刻みの時計(engine/FixedStepper.h)のテスト(PHY-1)。
#include "engine/FixedStepper.h"

#include "engine/Application.h"

#include <doctest/doctest.h>

using husky::engine::FixedStepper;

TEST_CASE("FixedStepper: dt が刻みと同じなら毎回 1 歩。短い dt は貯めて、たまった分だけ進める")
{
    FixedStepper sameAsStep; // 1/60 秒
    for (int i = 0; i < 100; ++i)
    {
        CHECK(sameAsStep.Advance(1.0 / 60.0) == 1); // 小数の誤差で 0 歩や 2 歩にならない
    }

    // 144 FPS の画面: 1 秒(144 フレーム)でちょうど 60 歩。歩かないフレームもある
    FixedStepper fast;
    int total = 0;
    int zeroFrames = 0;
    for (int i = 0; i < 144; ++i)
    {
        const int steps = fast.Advance(1.0 / 144.0);
        total += steps;
        zeroFrames += steps == 0 ? 1 : 0;
        CHECK(fast.GetAlpha() >= 0.0);
        CHECK(fast.GetAlpha() < 1.0);
    }
    CHECK(total == 60);
    CHECK(zeroFrames > 0);

    // 30 FPS の画面: 1 フレームに 2 歩
    FixedStepper slow;
    CHECK(slow.Advance(1.0 / 30.0) == 2);
}

TEST_CASE("FixedStepper: 1 フレームの歩数には上限があり、追いつけない分は捨てる。dt が 0 以下なら進めない")
{
    FixedStepper stepper(1.0 / 60.0, 4);
    CHECK(stepper.Advance(1.0) == 4); // 60 歩分たまっても 4 歩だけ
    CHECK(stepper.GetAlpha() == 0.0); // 追いつけない残りは捨てる(次のフレームに持ち越さない)
    CHECK(stepper.Advance(0.0) == 0);
    CHECK(stepper.Advance(-1.0) == 0);
    stepper.Reset();
    CHECK(stepper.GetAlpha() == 0.0);
}

TEST_CASE("FixedStepper: Application の当たり判定の世界は、決まった刻みで進む")
{
    husky::engine::Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    app.StepFrame(0, {});
    CHECK(app.GetPhysics2D().GetLastStepCount() == 1);
    CHECK(app.GetPhysics2D().GetStepper().GetStepSeconds() == doctest::Approx(1.0 / 60.0));
}
