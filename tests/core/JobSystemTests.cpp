// JobSystem(J-1: ワーカー 0 本の同期実行)のテスト。実行の順番が決まっているので、結果は毎回同じになる。
#include "core/JobSystem.h"

#include "core/MemoryTracker.h"

#include <doctest/doctest.h>

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
    // 1回目: 行列(vector)が大きくなるので、確保が起きてよい。
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
