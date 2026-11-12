// MemoryTracker.h の実装。
//
// 確保したメモリの形:
//   [余白(大きなアラインメントのときだけ) | 見出し 16 バイト | 中身(返すアドレス) size バイト | 余り(大きなアラインメントのときだけ)]
//   ↑ malloc が返したアドレス(raw)
// 見出しには「中身の大きさ」と「raw から中身までの距離」などを書く。返すときに、これで raw と大きさが分かる。
// ASan のビルドでは、見出しと余りに「触ったらエラー」の印を付ける(中身の前後のはみ出しを止めるため)。
//
// malloc / free を直接使ってよいのは、CLAUDE.md で、このファイルの中だけに許可している。
#include "core/MemoryTracker.h"

#include <atomic>  // std::atomic(複数のスレッドから同時に読み書きしても壊れない変数)
#include <cstdlib> // std::malloc, std::free

namespace husky::core
{
    namespace
    {
        // 中身の直前に置く見出し。16 バイトにして、中身の先頭が 16 の倍数のままになるようにする。
        struct AllocationHeader
        {
            std::size_t size;          // 頼まれた大きさ(数えるのに使う)
            std::uint32_t offset;      // raw から中身の先頭までのバイト数(余白 + 見出し)
            std::uint32_t trailing;    // 中身の後ろの余りのバイト数
        };
        static_assert(sizeof(AllocationHeader) == 16, "見出しは 16 バイトであること");

        constexpr std::size_t HeaderSize = sizeof(AllocationHeader);
        // malloc が保証するアラインメント(x64 の MSVC と、Mac の Clang では 16)。これ以下なら余白は要らない。
        constexpr std::size_t MallocAlignment = 16;

        // 集計の値。プログラムが始まる前(ほかのグローバル変数の初期化の途中)にも new は呼ばれるので、
        // 「定数で初期化される」(実行時の初期化を待たずに 0 になっている)std::atomic を使う。
        std::atomic<std::size_t> g_currentBytes{0};
        std::atomic<std::size_t> g_currentCount{0};
        std::atomic<std::size_t> g_peakBytes{0};
        std::atomic<std::uint64_t> g_totalAllocations{0};

        // 最大値を更新する。ほかのスレッドが同時に更新しても正しくなるように、
        // 「読んだ値のままなら書き換える」(compare_exchange)を、成功するか、より大きい値になるまで繰り返す。
        void UpdatePeak(std::size_t current)
        {
            std::size_t peak = g_peakBytes.load(std::memory_order_relaxed);
            while (current > peak && !g_peakBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed))
            {
            }
        }

        // value を alignment(2 の累乗)の倍数に切り上げる。
        std::uintptr_t AlignUp(std::uintptr_t value, std::size_t alignment)
        {
            return (value + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
        }
    }

    MemoryStats GetMemoryStats()
    {
        // memory_order_relaxed: 値そのものが壊れなければよく、ほかの変数との順番はそろえなくてよい、という指定(いちばん軽い)。
        MemoryStats stats;
        stats.currentBytes = g_currentBytes.load(std::memory_order_relaxed);
        stats.currentCount = g_currentCount.load(std::memory_order_relaxed);
        stats.peakBytes = g_peakBytes.load(std::memory_order_relaxed);
        stats.totalAllocations = g_totalAllocations.load(std::memory_order_relaxed);
        return stats;
    }

    void* TrackedAllocate(std::size_t size, std::size_t alignment)
    {
        if (alignment < MallocAlignment)
        {
            alignment = MallocAlignment;
        }
        // 大きなアラインメントのときは、ずらす分の余白を足して借りる(malloc は 16 の倍数までしか保証しないため)。
        const std::size_t extra = alignment > MallocAlignment ? alignment - MallocAlignment : 0;
        const std::size_t total = HeaderSize + extra + size;
        if (total < size) // 足し算があふれた(とても大きな size)
        {
            return nullptr;
        }

        void* raw = std::malloc(total);
        if (raw == nullptr)
        {
            return nullptr;
        }

        // 中身の先頭: 見出しの分だけ後ろで、alignment の倍数になる最初のアドレス。
        const auto rawAddress = reinterpret_cast<std::uintptr_t>(raw);
        const std::uintptr_t payloadAddress = AlignUp(rawAddress + HeaderSize, alignment);
        auto* payload = reinterpret_cast<std::byte*>(payloadAddress);
        auto* header = reinterpret_cast<AllocationHeader*>(payload - HeaderSize);
        header->size = size;
        header->offset = static_cast<std::uint32_t>(payloadAddress - rawAddress);
        header->trailing = static_cast<std::uint32_t>(total - header->offset - size);

        // ASan: 中身より前(余白と見出し)と、中身より後ろ(余り)を「触ったらエラー」にする。
        HUSKY_ASAN_POISON(raw, header->offset);
        HUSKY_ASAN_POISON(payload + size, header->trailing);

        // 数える。fetch_add は「足して、足す前の値を返す」を、ほかのスレッドと混ざらずに1回で行う。
        const std::size_t current = g_currentBytes.fetch_add(size, std::memory_order_relaxed) + size;
        g_currentCount.fetch_add(1, std::memory_order_relaxed);
        g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
        UpdatePeak(current);
        return payload;
    }

    void TrackedFree(void* pointer)
    {
        if (pointer == nullptr)
        {
            return;
        }
        auto* payload = static_cast<std::byte*>(pointer);
        const auto* header = reinterpret_cast<const AllocationHeader*>(payload - HeaderSize);
        const std::size_t size = header->size;
        const std::size_t offset = header->offset;
        const std::size_t trailing = header->trailing;
        std::byte* raw = payload - offset;

        g_currentBytes.fetch_sub(size, std::memory_order_relaxed);
        g_currentCount.fetch_sub(1, std::memory_order_relaxed);

        // ASan: 自分で付けた印を外してから、malloc に返す。
        HUSKY_ASAN_UNPOISON(raw, offset + size + trailing);
        std::free(raw);
    }
}
