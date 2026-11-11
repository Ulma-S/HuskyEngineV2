// FreeListAllocator.h の実装。
#include "core/FreeListAllocator.h"

#include <cstdint> // std::uintptr_t

namespace husky::core
{
    namespace
    {
        // ヘッダの目印。ただの「ありそうにない数」で、壊れたポインタや二重解放を見分けるのに使う。
        constexpr std::size_t UsedMagic = 0xA110CA7EDB10CC01ull; // 使用中
        constexpr std::size_t FreeMagic = 0xF4EEB10CF4EEB10Cull; // 空き

        // value を alignment の倍数に切り上げる(alignment は 2 の累乗であること)。
        // 例: AlignUp(20, 16) = 32。
        // ~(alignment - 1) は「下位の何ビットかを 0 にする」マスク(16 なら下位4ビット)。
        constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }
    }

    // ブロックの見出し。ちょうど 16 バイトにして、中身の先頭も 16 の倍数のアドレスになるようにする。
    struct FreeListAllocator::BlockHeader
    {
        std::size_t size;  // ヘッダを含むブロック全体の大きさ(16 の倍数)
        std::size_t magic; // UsedMagic か FreeMagic
    };
    static_assert(sizeof(std::size_t) == 8, "64ビット環境を前提にしている");

    namespace
    {
        constexpr std::size_t HeaderSize = 16;
        // ブロックの最小の大きさ: ヘッダ + 空きのときに「次の空き」を書く場所(16 にそろえる)。
        constexpr std::size_t MinBlockSize = HeaderSize + 16;
    }

    FreeListAllocator::FreeListAllocator(std::size_t capacityBytes)
        : m_capacity(capacityBytes & ~(MaxAlignment - 1)) // 16 の倍数に切り捨て
    {
        static_assert(sizeof(BlockHeader) == HeaderSize, "ヘッダは 16 バイトであること");
        if (m_capacity < MinBlockSize)
        {
            m_capacity = 0;
            return; // 小さすぎて1ブロックも作れない。Allocate は常に nullptr を返す
        }

        // メモリの塊をもらう。std::byte[] の unique_ptr なので、生の new / delete は書かずに済む。
        // new で確保した配列の先頭は、少なくとも 16 の倍数のアドレスになる(MSVC の x64 の場合)。
        m_memory = std::make_unique<std::byte[]>(m_capacity);

        // 最初は、塊全体が1つの大きな空きブロック。
        // reinterpret_cast: 「このバイト列の先頭を、BlockHeader として扱う」。
        BlockHeader* whole = reinterpret_cast<BlockHeader*>(m_memory.get());
        whole->size = m_capacity;
        whole->magic = FreeMagic;
        NextFree(whole) = nullptr;
        m_freeList = whole;
    }

    FreeListAllocator::BlockHeader*& FreeListAllocator::NextFree(BlockHeader* block)
    {
        // ヘッダのすぐ後ろ(中身の先頭)を、「BlockHeader* を入れる場所」として扱う。
        // 戻り値が参照(&)なので、NextFree(block) = x; と書けば、その場所に書き込める。
        return *reinterpret_cast<BlockHeader**>(reinterpret_cast<std::byte*>(block) + HeaderSize);
    }

    void* FreeListAllocator::Allocate(std::size_t size, std::size_t alignment)
    {
        if (alignment > MaxAlignment || m_capacity == 0)
        {
            return nullptr;
        }
        if (size == 0)
        {
            size = 1;
        }

        // 必要なブロックの大きさ = ヘッダ + 中身(16 の倍数に切り上げ)。最小の大きさ未満にはしない。
        std::size_t needed = HeaderSize + AlignUp(size, MaxAlignment);
        if (needed < MinBlockSize)
        {
            needed = MinBlockSize;
        }

        // ファーストフィット: 空きリストを先頭から見て、最初に収まるブロックを探す。
        // previous は「1つ前の空きブロック」。リストから外すときに、前のブロックのつなぎを書き換えるのに使う。
        BlockHeader* previous = nullptr;
        for (BlockHeader* block = m_freeList; block != nullptr; previous = block, block = NextFree(block))
        {
            if (block->size < needed)
            {
                continue; // 小さすぎる。次へ
            }

            BlockHeader* next = NextFree(block);

            // 余りが1ブロック分以上あれば、後ろを切り出して新しい空きブロックにする(分割)。
            if (block->size - needed >= MinBlockSize)
            {
                BlockHeader* rest = reinterpret_cast<BlockHeader*>(reinterpret_cast<std::byte*>(block) + needed);
                rest->size = block->size - needed;
                rest->magic = FreeMagic;
                NextFree(rest) = next;
                next = rest; // 空きリストの中で、block の代わりに rest がつながる
                block->size = needed;
            }

            // block を空きリストから外す。
            if (previous != nullptr)
            {
                NextFree(previous) = next;
            }
            else
            {
                m_freeList = next;
            }

            block->magic = UsedMagic;
            m_usedBytes += block->size;
            return reinterpret_cast<std::byte*>(block) + HeaderSize; // 中身の先頭を返す
        }

        return nullptr; // 収まる空きが無い
    }

    FreeListAllocator::BlockHeader* FreeListAllocator::FindAllocatedHeader(void* pointer) const
    {
        if (pointer == nullptr || m_memory == nullptr)
        {
            return nullptr;
        }
        // ポインタを整数(アドレス)として扱い、塊の範囲内かを確かめる。
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(m_memory.get());
        const auto end = begin + m_capacity;
        if (address < begin + HeaderSize || address >= end || (address - begin) % MaxAlignment != 0)
        {
            return nullptr;
        }
        // 中身の先頭の 16 バイト手前がヘッダ。目印が「使用中」でなければ、貸し出したものではない(または二重解放)。
        BlockHeader* header = reinterpret_cast<BlockHeader*>(static_cast<std::byte*>(pointer) - HeaderSize);
        return header->magic == UsedMagic ? header : nullptr;
    }

    bool FreeListAllocator::Free(void* pointer)
    {
        BlockHeader* block = FindAllocatedHeader(pointer);
        if (block == nullptr)
        {
            return false;
        }
        m_usedBytes -= block->size;
        block->magic = FreeMagic;

        // 空きリストは「アドレスの小さい順」に並べてある。block を差し込む場所(previous と next の間)を探す。
        BlockHeader* previous = nullptr;
        BlockHeader* next = m_freeList;
        while (next != nullptr && next < block)
        {
            previous = next;
            next = NextFree(next);
        }

        // 差し込む。
        NextFree(block) = next;
        if (previous != nullptr)
        {
            NextFree(previous) = block;
        }
        else
        {
            m_freeList = block;
        }

        // 後ろとの結合: block のすぐ後ろが next なら、1つにくっつける。
        auto endOf = [](BlockHeader* b) { return reinterpret_cast<std::byte*>(b) + b->size; };
        if (next != nullptr && endOf(block) == reinterpret_cast<std::byte*>(next))
        {
            block->size += next->size;
            NextFree(block) = NextFree(next);
        }

        // 前との結合: previous のすぐ後ろが block なら、1つにくっつける。
        if (previous != nullptr && endOf(previous) == reinterpret_cast<std::byte*>(block))
        {
            previous->size += block->size;
            NextFree(previous) = NextFree(block);
        }
        return true;
    }

    std::size_t FreeListAllocator::GetFreeBlockCount() const
    {
        std::size_t count = 0;
        for (BlockHeader* block = m_freeList; block != nullptr; block = NextFree(block))
        {
            ++count;
        }
        return count;
    }

    std::size_t FreeListAllocator::GetLargestFreeSize() const
    {
        std::size_t largest = 0;
        for (BlockHeader* block = m_freeList; block != nullptr; block = NextFree(block))
        {
            if (block->size - HeaderSize > largest)
            {
                largest = block->size - HeaderSize;
            }
        }
        return largest;
    }
}
