// FreeListAllocator(フリーリスト型のヒープアロケータ)のテスト。
#include "core/FreeListAllocator.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring> // std::memset
#include <random>
#include <vector>

using husky::core::FreeListAllocator;

namespace
{
    constexpr std::size_t Capacity = 4096;

    // アドレスが alignment の倍数か。
    bool IsAligned(const void* pointer, std::size_t alignment)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
    }
}

TEST_CASE("FreeListAllocator: 最初は全体が1つの空きブロック")
{
    FreeListAllocator allocator(Capacity);
    CHECK(allocator.GetCapacity() == Capacity);
    CHECK(allocator.GetUsedBytes() == 0);
    CHECK(allocator.GetFreeBlockCount() == 1);
    CHECK(allocator.GetLargestFreeSize() == Capacity - 16); // ヘッダの 16 バイトを除いた分
}

TEST_CASE("FreeListAllocator: 確保したアドレスは 16 の倍数で、ヘッダ分を含めて使用量が増える")
{
    FreeListAllocator allocator(Capacity);
    void* a = allocator.Allocate(20); // 中身は 32 に切り上げ。ヘッダ 16 を足して 48
    REQUIRE(a != nullptr);
    CHECK(IsAligned(a, 16));
    CHECK(allocator.GetUsedBytes() == 48);

    void* b = allocator.Allocate(1); // 最小の 32
    REQUIRE(b != nullptr);
    CHECK(IsAligned(b, 16));
    CHECK(allocator.GetUsedBytes() == 48 + 32);
}

TEST_CASE("FreeListAllocator: 確保した領域どうしは重ならない")
{
    FreeListAllocator allocator(Capacity);
    std::vector<unsigned char*> blocks;
    for (int i = 0; i < 10; ++i)
    {
        auto* block = static_cast<unsigned char*>(allocator.Allocate(100));
        REQUIRE(block != nullptr);
        std::memset(block, i, 100); // ブロックごとに違う値で埋める
        blocks.push_back(block);
    }
    // ほかのブロックに上書きされていなければ、それぞれ自分の値のまま。
    for (int i = 0; i < 10; ++i)
    {
        for (int j = 0; j < 100; ++j)
        {
            if (blocks[static_cast<std::size_t>(i)][j] != i)
            {
                FAIL("ブロック ", i, " の ", j, " バイト目が書き換わっている");
            }
        }
    }
}

TEST_CASE("FreeListAllocator: 解放した場所は再利用される")
{
    FreeListAllocator allocator(Capacity);
    void* a = allocator.Allocate(64);
    CHECK(allocator.Free(a));
    CHECK(allocator.GetUsedBytes() == 0);

    void* b = allocator.Allocate(64);
    CHECK(b == a); // ファーストフィットなので、先頭の同じ場所が選ばれる
}

TEST_CASE("FreeListAllocator: 隣どうしの空きは結合され、全部解放すると1つの空きに戻る")
{
    FreeListAllocator allocator(Capacity);
    void* a = allocator.Allocate(64);
    void* b = allocator.Allocate(64);
    void* c = allocator.Allocate(64);
    // メモリ上の並び: [a][b][c][残りの空き]

    allocator.Free(a);
    allocator.Free(c);
    // [空き a][b][空き c + 残り] → c は後ろの残りと結合されるので、空きは2つ
    CHECK(allocator.GetFreeBlockCount() == 2);

    allocator.Free(b);
    // b の前(a)と後ろ(c + 残り)の両方と結合され、全体が1つの空きに戻る
    CHECK(allocator.GetFreeBlockCount() == 1);
    CHECK(allocator.GetLargestFreeSize() == Capacity - 16);
    CHECK(allocator.GetUsedBytes() == 0);
}

TEST_CASE("FreeListAllocator: 断片化: 空きの合計は足りていても、大きなものが入らないことがある")
{
    FreeListAllocator allocator(1024);
    std::vector<void*> blocks;
    // 64 バイト(ブロックは 80 バイト)を、入るだけ確保する。
    while (void* block = allocator.Allocate(64))
    {
        blocks.push_back(block);
    }
    // 1つおきに解放する → 空きが細切れになる
    for (std::size_t i = 0; i < blocks.size(); i += 2)
    {
        allocator.Free(blocks[i]);
    }
    const std::size_t totalFree = allocator.GetCapacity() - allocator.GetUsedBytes();
    CHECK(totalFree > 200);                          // 合計では十分に空いている
    CHECK(allocator.GetLargestFreeSize() < 200);     // でも一番大きな空きは小さい
    CHECK(allocator.Allocate(200) == nullptr);       // だから 200 バイトは入らない
}

TEST_CASE("FreeListAllocator: 容量を超える要求は nullptr")
{
    FreeListAllocator allocator(Capacity);
    CHECK(allocator.Allocate(Capacity) == nullptr); // ヘッダの分だけ足りない
    CHECK(allocator.Allocate(Capacity - 16) != nullptr);
    CHECK(allocator.Allocate(1) == nullptr); // もう空きが無い
}

TEST_CASE("FreeListAllocator: 不正な解放は false を返し、何もしない")
{
    FreeListAllocator allocator(Capacity);
    void* a = allocator.Allocate(64);

    CHECK_FALSE(allocator.Free(nullptr));

    int notMine = 0;
    CHECK_FALSE(allocator.Free(&notMine)); // このアロケータのものではない

    CHECK(allocator.Free(a));
    CHECK_FALSE(allocator.Free(a)); // 二重解放
    CHECK(allocator.GetUsedBytes() == 0);
}

TEST_CASE("FreeListAllocator: 確保と解放をランダムに1万回繰り返しても、中身が壊れず、最後は1つの空きに戻る")
{
    FreeListAllocator allocator(64 * 1024);

    struct Live
    {
        unsigned char* pointer;
        std::size_t size;
        unsigned char fill; // 書き込んだ値
    };
    std::vector<Live> live;
    std::mt19937 random(2026); // 種を固定して、毎回同じ手順にする
    std::uniform_int_distribution<std::size_t> randomSize(1, 300);

    for (int step = 0; step < 10000; ++step)
    {
        const bool doAllocate = live.empty() || (random() % 2 == 0);
        if (doAllocate)
        {
            const std::size_t size = randomSize(random);
            auto* pointer = static_cast<unsigned char*>(allocator.Allocate(size));
            if (pointer != nullptr)
            {
                const auto fill = static_cast<unsigned char>(step);
                std::memset(pointer, fill, size);
                live.push_back(Live{pointer, size, fill});
            }
        }
        else
        {
            // ランダムに1つ選び、中身が壊れていないか確かめてから解放する。
            const std::size_t index = random() % live.size();
            const Live target = live[index];
            for (std::size_t i = 0; i < target.size; ++i)
            {
                if (target.pointer[i] != target.fill)
                {
                    FAIL("step ", step, ": 確保中のブロックの中身が書き換わっている");
                }
            }
            REQUIRE(allocator.Free(target.pointer));
            live[index] = live.back(); // 末尾と入れ替えて消す(順番は気にしない)
            live.pop_back();
        }
    }

    // 残りを全部解放すると、断片が全部結合されて1つに戻る。
    for (const Live& block : live)
    {
        REQUIRE(allocator.Free(block.pointer));
    }
    CHECK(allocator.GetUsedBytes() == 0);
    CHECK(allocator.GetFreeBlockCount() == 1);
    CHECK(allocator.GetLargestFreeSize() == allocator.GetCapacity() - 16);
}

TEST_CASE("FreeListAllocator: 16 より大きいアラインメントには対応しない")
{
    FreeListAllocator allocator(Capacity);
    CHECK(allocator.Allocate(64, 32) == nullptr);
    CHECK(allocator.Allocate(64, 8) != nullptr); // 16 以下なら OK
}
