// FrameClock のテスト。時刻は引数で渡すので、実際の時間を待たずに、毎回同じ結果で確かめられる。
#include "engine/FrameClock.h"

#include <doctest/doctest.h>

#include <cstdint>

using husky::engine::FrameClock;

namespace
{
    // 秒 → ナノ秒。テストを読みやすくするための小さな関数。
    // constexpr: コンパイル時にも計算できる関数。
    constexpr std::uint64_t Seconds(double seconds)
    {
        return static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
    }
}

// TEST_CASE はテスト1つ分。"" の中は、何を確かめるかを文章で書く(失敗したときに表示される)。
TEST_CASE("FrameClock: 最初の Tick は、前の時刻がないので 0 を返す")
{
    FrameClock clock;
    // CHECK は「この条件が成り立つはず」という確認。成り立たないと、その行と値が表示され、テストは失敗になる。
    CHECK(clock.Tick(Seconds(123.0)) == 0.0);
}

TEST_CASE("FrameClock: dt は前回の Tick からの経過時間(秒)")
{
    FrameClock clock;
    clock.Tick(Seconds(10.0));

    // 小数は計算の誤差でぴったり一致しないことがあるので、doctest::Approx で「ほぼ等しい」を確かめる。
    CHECK(clock.Tick(Seconds(10.016)) == doctest::Approx(0.016));
    CHECK(clock.Tick(Seconds(10.050)) == doctest::Approx(0.034));
    CHECK(clock.GetDeltaSeconds() == doctest::Approx(0.034));
}

TEST_CASE("FrameClock: dt は上限で切り詰められる(ブレークポイントで止めた後など)")
{
    FrameClock clock(0.1); // 上限 0.1 秒
    clock.Tick(Seconds(0.0));

    CHECK(clock.Tick(Seconds(10.0)) == doctest::Approx(0.1)); // 10 秒たっても 0.1 秒
    CHECK(clock.Tick(Seconds(10.05)) == doctest::Approx(0.05)); // 上限より短ければ、そのまま
}

TEST_CASE("FrameClock: 固定 dt を設定すると、実際の時間に関係なく毎回その値になる")
{
    FrameClock clock;
    clock.SetFixedDelta(1.0 / 60.0);

    // 最初の Tick も、時間が大きく飛んでも、すべて 1/60 秒。テストやリプレイで結果を毎回同じにするため。
    CHECK(clock.Tick(Seconds(0.0)) == doctest::Approx(1.0 / 60.0));
    CHECK(clock.Tick(Seconds(0.001)) == doctest::Approx(1.0 / 60.0));
    CHECK(clock.Tick(Seconds(999.0)) == doctest::Approx(1.0 / 60.0));
}

TEST_CASE("FrameClock: 固定 dt を 0 にすると、実際の時間に戻る")
{
    FrameClock clock;
    clock.SetFixedDelta(1.0 / 60.0);
    clock.Tick(Seconds(1.0));

    clock.SetFixedDelta(0.0);
    CHECK(clock.Tick(Seconds(1.02)) == doctest::Approx(0.02));
}

TEST_CASE("FrameClock: 固定 dt も上限で切り詰められる")
{
    FrameClock clock(0.1);
    clock.SetFixedDelta(0.5);
    CHECK(clock.Tick(Seconds(0.0)) == doctest::Approx(0.1));
}
