// JobSystem のテスト。前半はワーカー 0 本(同期実行): 実行の順番が決まっているので、結果は毎回同じになる。
#include "core/JobSystem.h"

#include "core/MemoryTracker.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono> // std::chrono::milliseconds(時間の長さ)
#include <thread> // std::this_thread::sleep_for
#include <vector>

using husky::core::JobCounter;
using husky::core::JobSystem;

TEST_CASE("JobSystem: Schedule は積むだけ。Wait で実行され、印が「終わった」になる")
{
    JobSystem jobs;
    JobCounter counter;
    CHECK(counter.IsDone()); // 何も積んでいなければ「終わっている」

    int value = 0;
    jobs.Schedule([&value] { value = 42; }, counter);
    CHECK(value == 0); // まだ実行されていない
    CHECK_FALSE(counter.IsDone());
    CHECK(jobs.GetPendingCount() == 1);

    jobs.Wait(counter);
    CHECK(value == 42);
    CHECK(counter.IsDone());
    CHECK(jobs.GetPendingCount() == 0);
}

TEST_CASE("JobSystem: 1つの印に、いくつでも積める。積んだ順に実行する")
{
    JobSystem jobs;
    JobCounter counter;
    std::vector<int> order;
    for (int i = 0; i < 5; ++i)
    {
        jobs.Schedule([&order, i] { order.push_back(i); }, counter);
    }
    jobs.Wait(counter);
    CHECK(order == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("JobSystem: Wait は、先に積まれたほかの印のジョブも、順番どおりに実行する(順番を飛ばさない)")
{
    JobSystem jobs;
    JobCounter first;
    JobCounter second;
    std::vector<char> order;
    jobs.Schedule([&order] { order.push_back('a'); }, first);
    jobs.Schedule([&order] { order.push_back('b'); }, second);
    jobs.Schedule([&order] { order.push_back('c'); }, first);

    jobs.Wait(second); // a(first)を実行してから b(second)。c はまだ残る
    CHECK(order == std::vector<char>{'a', 'b'});
    CHECK(second.IsDone());
    CHECK_FALSE(first.IsDone());

    jobs.Wait(first);
    CHECK(order == std::vector<char>{'a', 'b', 'c'});
}

TEST_CASE("JobSystem: ジョブの中から積んだジョブも、同じ印なら Wait が待つ")
{
    JobSystem jobs;
    JobCounter counter;
    std::vector<int> order;
    // 親のジョブが、子のジョブを 3 つ積む(行列が大きくなり、中身が引っ越しても壊れないこと)。
    jobs.Schedule(
        [&] {
            order.push_back(0);
            for (int i = 1; i <= 3; ++i)
            {
                jobs.Schedule([&order, i] { order.push_back(i); }, counter);
            }
        },
        counter);
    jobs.Wait(counter);
    CHECK(order == std::vector<int>{0, 1, 2, 3});
    CHECK(counter.IsDone());
}

TEST_CASE("JobSystem: RunPendingJobs はすべて実行する。壊すときも、残りを実行してから壊れる")
{
    int runs = 0;
    JobCounter counter; // JobSystem より先に宣言する(後に壊れる)。ジョブが終わるまで印を壊さないため
    {
        JobSystem jobs;
        jobs.Schedule([&runs] { ++runs; }, counter);
        jobs.Schedule([&runs] { ++runs; }, counter);
        CHECK(jobs.RunPendingJobs() == 2);
        CHECK(runs == 2);

        jobs.Schedule([&runs] { ++runs; }, counter);
    } // ← ここで JobSystem が壊れるとき、残っていた 1 つを実行する
    CHECK(runs == 3);
    CHECK(counter.IsDone());
}

TEST_CASE("JobSystem: 慣らした後は、小さなジョブを積んで待っても、メモリを確保しない")
{
    using husky::core::GetMemoryStats;

    JobSystem jobs;
    JobCounter counter;
    int sum = 0;
    // 1回目: 慣らし(行列の場所は JobSystem を作ったときに確保済みだが、念のため1回まわしてから数える)。
    for (int i = 0; i < 64; ++i)
    {
        jobs.Schedule([&sum] { ++sum; }, counter);
    }
    jobs.Wait(counter);

    // 2回目以降: 行列のメモリを使い回し、std::function も小さなラムダ式(ポインタ1つ)なら自分の中にしまうので、確保は 0 回。
    const std::uint64_t before = GetMemoryStats().totalAllocations;
    for (int frame = 0; frame < 10; ++frame)
    {
        for (int i = 0; i < 64; ++i)
        {
            jobs.Schedule([&sum] { ++sum; }, counter);
        }
        jobs.Wait(counter);
    }
    const std::uint64_t after = GetMemoryStats().totalAllocations;
    CHECK(after == before);
    CHECK(sum == 64 * 11);
}

// ---- ここから下は、ワーカースレッドを使うテスト ----
// 実行の順番は毎回変わるので、「順番」ではなく「すべて実行された」「結果が見える」「止まらない」ことを確かめる。
// Windows(MSVC)では TSan が使えないので、ASan のビルドでも回して、メモリの誤用が無いことを確かめる(CLAUDE.md)。

namespace
{
    constexpr std::size_t TestWorkerCount = 4;
}

TEST_CASE("JobSystem(ワーカーあり): Wait を呼ばなくても、ワーカーがジョブを実行する")
{
    JobSystem jobs(TestWorkerCount);
    CHECK(jobs.GetWorkerCount() == TestWorkerCount);

    JobCounter counter;
    std::atomic<bool> ran{false};
    jobs.Schedule([&ran] { ran.store(true); }, counter);

    // このスレッドはジョブを実行しない。ワーカーが終わらせるのを、最大 5 秒まで待つ。
    for (int i = 0; i < 500 && !counter.IsDone(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(counter.IsDone());
    CHECK(ran.load());
}

TEST_CASE("JobSystem(ワーカーあり): たくさんのジョブが、すべて1回ずつ実行される")
{
    JobSystem jobs(TestWorkerCount);
    JobCounter counter;
    constexpr int JobCount = 10000;
    std::atomic<int> runs{0};
    for (int i = 0; i < JobCount; ++i)
    {
        jobs.Schedule([&runs] { runs.fetch_add(1, std::memory_order_relaxed); }, counter);
    }
    jobs.Wait(counter);
    CHECK(runs.load() == JobCount);
    CHECK(jobs.GetPendingCount() == 0);
}

TEST_CASE("JobSystem(ワーカーあり): ジョブが書いた結果は、Wait が戻った後に読める(鍵の無い、ふつうの変数でも)")
{
    JobSystem jobs(TestWorkerCount);
    JobCounter counter;
    // ジョブごとに書く場所を分ける(同じ場所を2つのジョブが書くと、データ競合になる)。
    std::vector<int> results(1000, 0);
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        int* slot = &results[i];
        const int value = static_cast<int>(i) * 3;
        jobs.Schedule([slot, value] { *slot = value; }, counter);
    }
    jobs.Wait(counter); // 印の release / acquire で、ジョブが書いた値がこのスレッドから見える

    bool allWritten = true;
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        allWritten = allWritten && results[i] == static_cast<int>(i) * 3;
    }
    CHECK(allWritten);
}

TEST_CASE("JobSystem(ワーカーあり): ワーカーの中から積んだジョブも、同じ印なら Wait が待つ")
{
    JobSystem jobs(TestWorkerCount);
    JobCounter counter;
    std::atomic<int> runs{0};
    for (int parent = 0; parent < 50; ++parent)
    {
        jobs.Schedule(
            [&jobs, &counter, &runs] {
                runs.fetch_add(1);
                for (int child = 0; child < 10; ++child)
                {
                    jobs.Schedule([&runs] { runs.fetch_add(1); }, counter);
                }
            },
            counter);
    }
    jobs.Wait(counter);
    CHECK(runs.load() == 50 + 50 * 10);
}

TEST_CASE("JobSystem(ワーカーあり): 積んで待つのを何度も繰り返しても、止まらず、メモリを確保しない")
{
    using husky::core::GetMemoryStats;

    JobSystem jobs(TestWorkerCount);
    std::atomic<int> runs{0};
    auto round = [&] {
        JobCounter counter;
        for (int i = 0; i < 100; ++i)
        {
            jobs.Schedule([&runs] { runs.fetch_add(1, std::memory_order_relaxed); }, counter);
        }
        jobs.Wait(counter);
    };

    round(); // 1回目(行列の場所は、JobSystem を作ったときに確保済み)
    const std::uint64_t before = GetMemoryStats().totalAllocations;
    for (int i = 0; i < 300; ++i)
    {
        round(); // 毎回、待っている間にワーカーとゲームスレッドが取り合う。止まれば(デッドロック)テストが終わらない
    }
    const std::uint64_t after = GetMemoryStats().totalAllocations;
    CHECK(runs.load() == 100 * 301);
    // 行列の場所は最初に確保してあり(InitialQueueCapacity)、std::function も小さなラムダ式なら自分の中にしまう。
    // ワーカーとの取り合いで行列の長さが毎回変わっても、確保は 0 回のまま。
    CHECK(after == before);
}

TEST_CASE("JobSystem(ワーカーあり): 壊すときは、積まれていたジョブをすべて実行してから、ワーカーを止める")
{
    std::atomic<int> runs{0};
    JobCounter counter; // JobSystem より先に宣言する(後に壊れる)
    {
        JobSystem jobs(TestWorkerCount);
        for (int i = 0; i < 1000; ++i)
        {
            jobs.Schedule([&runs] { runs.fetch_add(1); }, counter);
        }
    } // ← ここで、残りを実行し、ワーカーの終わりを待つ
    CHECK(runs.load() == 1000);
    CHECK(counter.IsDone());
}

TEST_CASE("JobSystem: ワーカーの数の目安は 1 以上")
{
    CHECK(JobSystem::RecommendedWorkerCount() >= 1);
}
