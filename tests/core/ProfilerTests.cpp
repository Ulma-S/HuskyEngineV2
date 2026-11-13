// 簡単な CPU プロファイラー(core/Profiler.h)のテスト。
// 時間そのものは毎回変わるので、数え方(区間の登録、足し方、フレームの区切り、平均と最大)を、決まった値を足して確かめる。
#include "core/Profiler.h"

#include <doctest/doctest.h>

using husky::core::ProfileScope;
using husky::core::Profiler;

namespace
{
    // HUSKY_PROFILE_SCOPE を書いた関数(開発ビルドでは呼ぶたびに区間 "test.macro" に1回足される)。
    void MeasuredFunction()
    {
        HUSKY_PROFILE_SCOPE("test.macro");
    }

    int FindSection(const char* name)
    {
        return Profiler::Get().RegisterSection(name); // 同じ名前なら同じ番号が返る
    }
}

TEST_CASE("Profiler: 同じ名前の区間は同じ番号。足した時間はフレームの区切りで「前のフレーム」になり、平均と最大も更新される")
{
    Profiler& profiler = Profiler::Get();
    const int a = profiler.RegisterSection("test.a");
    const int b = profiler.RegisterSection("test.b");
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(a != b);
    CHECK(profiler.RegisterSection("test.a") == a);

    profiler.EndFrame(); // ほかのテストで足されたものを流す
    // このフレーム: a に 2 ミリ秒を 2 回、b に 1 ミリ秒
    profiler.Add(a, 2'000'000);
    profiler.Add(a, 2'000'000);
    profiler.Add(b, 1'000'000);
    profiler.EndFrame();
    CHECK(profiler.GetStats(a).lastMs == doctest::Approx(4.0));
    CHECK(profiler.GetStats(a).lastCalls == 2);
    CHECK(profiler.GetStats(b).lastMs == doctest::Approx(1.0));
    CHECK(profiler.GetStats(a).maxMs >= 4.0);
    const double averageBefore = profiler.GetStats(a).averageMs;

    // 次のフレームは何も足さない: 前のフレームは 0、平均は少し下がり、最大はそのまま
    profiler.EndFrame();
    CHECK(profiler.GetStats(a).lastMs == 0.0);
    CHECK(profiler.GetStats(a).lastCalls == 0);
    CHECK(profiler.GetStats(a).averageMs < averageBefore);
    CHECK(profiler.GetStats(a).maxMs >= 4.0);
    // 最大を 0 に戻せる(毎秒のログの後)
    profiler.ResetMax();
    CHECK(profiler.GetStats(a).maxMs == 0.0);
    // 知らない番号には足さない(止まらない)
    profiler.Add(-1, 100);
    profiler.Add(Profiler::MaxSections + 5, 100);
}

TEST_CASE("Profiler: ProfileScope はブロックを抜けるときに1回分を足す(RAII)。マクロは開発ビルドで同じことをする")
{
    Profiler& profiler = Profiler::Get();
    const int scoped = profiler.RegisterSection("test.scope");
    profiler.EndFrame();
    {
        const ProfileScope scope(scoped); // ここで時計を読み、ブロックの終わりで差を足す
        volatile int work = 0;             // 何か少し仕事をする(volatile: 消されないように)
        for (int i = 0; i < 1000; ++i)
        {
            work = work + i;
        }
    }
    MeasuredFunction();
    MeasuredFunction();
    profiler.EndFrame();
    CHECK(profiler.GetStats(scoped).lastCalls == 1);
    CHECK(profiler.GetStats(scoped).lastMs >= 0.0);
#if HUSKY_DEVELOPMENT
    CHECK(profiler.GetStats(FindSection("test.macro")).lastCalls == 2);
#else
    CHECK(profiler.GetStats(FindSection("test.macro")).lastCalls == 0); // 製品ではマクロは何もしない
#endif
}
