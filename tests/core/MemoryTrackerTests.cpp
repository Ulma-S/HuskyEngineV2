// MemoryTracker(汎用のヒープの入口と、使用量の集計)のテスト。
// テストの exe にも GlobalOperatorNew.cpp が入っているので、new や std::vector はすべて MemoryTracker を通る。
//
// 注意: doctest の CHECK も、中でメモリを確保することがある。
// そのため、「前の値を取る → 調べたい処理 → 後の値を取る」を、CHECK を挟まずに続けて行い、差を最後に確かめる。
#include "core/AddressSanitizer.h"
#include "core/MemoryTracker.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <vector>

using husky::core::GetMemoryStats;
using husky::core::MemoryStats;
using husky::core::TrackedAllocate;
using husky::core::TrackedFree;

namespace
{
    bool IsAligned(const void* pointer, std::size_t alignment)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
    }

    // 大きなアラインメント(64)を指定した型。new すると std::align_val_t 付きの operator new が呼ばれる。
    struct alignas(64) CacheLine
    {
        unsigned char bytes[64];
    };
}

TEST_CASE("MemoryTracker: make_unique の確保と解放が、量と件数に数えられる")
{
    const MemoryStats before = GetMemoryStats();
    auto value = std::make_unique<std::int64_t>(42);
    const MemoryStats during = GetMemoryStats();
    value.reset(); // 解放
    const MemoryStats after = GetMemoryStats();

    CHECK(during.currentCount == before.currentCount + 1);
    CHECK(during.currentBytes == before.currentBytes + sizeof(std::int64_t));
    CHECK(during.totalAllocations == before.totalAllocations + 1);
    CHECK(after.currentCount == before.currentCount);
    CHECK(after.currentBytes == before.currentBytes);
    CHECK(after.totalAllocations == during.totalAllocations); // 解放では、確保の回数は増えない
}

TEST_CASE("MemoryTracker: std::vector の中身も数えられる")
{
    const MemoryStats before = GetMemoryStats();
    MemoryStats during;
    {
        std::vector<int> values(1000);
        during = GetMemoryStats();
    } // ← ここで values が消え、中身のメモリが返される
    // (Debug の標準ライブラリは、範囲外チェックのために vector ごとに管理用のメモリ(16 バイト)を別に確保する。
    //   これは vector そのものが消えるまで残るので、clear や shrink_to_fit ではなく、消えた後で比べる)
    const MemoryStats after = GetMemoryStats();

    CHECK(during.currentBytes >= before.currentBytes + 1000 * sizeof(int));
    CHECK(after.currentBytes == before.currentBytes);
    CHECK(during.peakBytes >= during.currentBytes); // 最大値は、今の値以上
}

TEST_CASE("MemoryTracker: 大きなアラインメントの型の new も、指定どおりのアドレスで数えられる")
{
    const MemoryStats before = GetMemoryStats();
    auto line = std::make_unique<CacheLine>();
    const MemoryStats during = GetMemoryStats();
    const bool aligned = IsAligned(line.get(), 64);
    line.reset();
    const MemoryStats after = GetMemoryStats();

    CHECK(aligned);
    CHECK(during.currentBytes == before.currentBytes + sizeof(CacheLine));
    CHECK(after.currentBytes == before.currentBytes);
}

TEST_CASE("MemoryTracker: TrackedAllocate はアラインメントを守り、0 バイトでも有効なアドレスを返す")
{
    void* big = TrackedAllocate(100, 256);
    void* small = TrackedAllocate(3, 1);
    void* empty = TrackedAllocate(0, 16);
    const bool bigAligned = IsAligned(big, 256);
    const bool smallAligned = IsAligned(small, 16); // 16 未満の指定でも、16 の倍数にそろえる
    TrackedFree(big);
    TrackedFree(small);
    TrackedFree(empty);
    TrackedFree(nullptr); // 何もしない

    CHECK(big != nullptr);
    CHECK(bigAligned);
    CHECK(smallAligned);
    CHECK(empty != nullptr);
}

TEST_CASE("MemoryTracker(ASan): 中身の前後のはみ出しは触れない")
{
    if (!husky::core::IsAddressSanitizerEnabled())
    {
        return; // ASan のビルドではない
    }
    using husky::core::IsPoisonedByAddressSanitizer;

    auto* bytes = static_cast<unsigned char*>(TrackedAllocate(20, 64));
    const bool first = IsPoisonedByAddressSanitizer(bytes);
    const bool last = IsPoisonedByAddressSanitizer(bytes + 19);
    const bool before = IsPoisonedByAddressSanitizer(bytes - 1); // 見出し
    const bool after = IsPoisonedByAddressSanitizer(bytes + 20); // 頼んだ大きさのすぐ外
    TrackedFree(bytes);

    CHECK_FALSE(first);
    CHECK_FALSE(last);
    CHECK(before);
    CHECK(after);
}
