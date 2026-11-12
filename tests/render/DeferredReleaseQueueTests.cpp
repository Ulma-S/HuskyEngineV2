// DeferredReleaseQueue(解放を数フレーム遅らせる)のテスト。GPU は使わない(int を「リソース」の代わりにする)。
#include "render/DeferredReleaseQueue.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using husky::render::DeferredReleaseQueue;

TEST_CASE("DeferredReleaseQueue: 手放してから FramesToWait フレーム経つまでは解放しない")
{
    DeferredReleaseQueue<int> queue;
    std::vector<int> released;
    auto release = [&released](int item) { released.push_back(item); };

    constexpr std::uint64_t wait = DeferredReleaseQueue<int>::FramesToWait;
    queue.Push(7, 10); // フレーム 10 で手放す

    // フレーム 10 〜 (10 + wait - 1) では、まだ GPU が使っているかもしれないので解放しない。
    for (std::uint64_t frame = 10; frame < 10 + wait; ++frame)
    {
        CHECK(queue.ReleaseReady(frame, release) == 0);
    }
    CHECK(released.empty());
    CHECK(queue.Size() == 1);

    // wait フレーム経ったら解放する。
    CHECK(queue.ReleaseReady(10 + wait, release) == 1);
    REQUIRE(released.size() == 1);
    CHECK(released[0] == 7);
    CHECK(queue.Size() == 0);

    // 一度解放したものは、2回解放しない。
    CHECK(queue.ReleaseReady(100, release) == 0);
    CHECK(released.size() == 1);
}

TEST_CASE("DeferredReleaseQueue: 古いものから解放し、新しいものは残す")
{
    DeferredReleaseQueue<int> queue;
    std::vector<int> released;
    auto release = [&released](int item) { released.push_back(item); };

    constexpr std::uint64_t wait = DeferredReleaseQueue<int>::FramesToWait;
    queue.Push(1, 0);
    queue.Push(2, 1);
    queue.Push(3, 0);
    queue.Push(4, 5);

    CHECK(queue.ReleaseReady(wait, release) == 2); // フレーム 0 に手放した 1 と 3
    CHECK(released == std::vector<int>{1, 3});
    CHECK(queue.Size() == 2);

    CHECK(queue.ReleaseReady(1 + wait, release) == 1); // 2
    CHECK(released == std::vector<int>{1, 3, 2});

    queue.ReleaseAll(release); // 終了のとき: 残りをすべて
    CHECK(released == std::vector<int>{1, 3, 2, 4});
    CHECK(queue.Size() == 0);
}
