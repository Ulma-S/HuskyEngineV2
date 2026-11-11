// ハンドルから中身を引くための表。中身は T の値として、この表が持つ。
//
// 仕組み:
//   - 中身は vector の「場所(スロット)」に入る。ハンドルの index はスロットの番号。
//   - スロットごとに世代を持つ。中身を消すと世代が1つ進む。
//   - 古いハンドル(世代が合わない)で問い合わせると、nullptr が返る。
//   - 空いたスロットは「空きリスト」につないでおき、次の Add で再利用する。
//
// 動作スレッド: ゲームスレッドのみ(複数のスレッドから同時に触ってはいけない)。
#pragma once

#include "core/Handle.h"

#include <cstddef>
#include <cstdint>
#include <optional> // std::optional: 「値があるか、無いか」を表す(C# の Nullable<T> に近い)
#include <utility>  // std::move
#include <vector>

namespace husky::core
{
    template <typename T>
    class HandleTable final
    {
    public:
        // 中身を加え、それを指すハンドルを返す。
        // std::move: value をコピーせずに、中身を「移動」して表に入れる(大きな値でも速い)。
        //   移動した後の value は空っぽ扱いになるので、それ以降は使わない。
        Handle<T> Add(T value)
        {
            std::uint32_t index = 0;
            if (m_firstFree != NoFreeSlot)
            {
                // 空いているスロットを再利用する。世代は Remove のときに進めてある。
                index = m_firstFree;
                m_firstFree = m_slots[index].nextFree;
            }
            else
            {
                // 空きが無ければ、末尾に新しいスロットを作る。世代は 1 から始める(0 は「無効」用)。
                index = static_cast<std::uint32_t>(m_slots.size());
                m_slots.push_back(Slot{});
                m_slots[index].generation = 1;
            }

            Slot& slot = m_slots[index]; // & : コピーではなく、vector の中のスロットそのものを触る
            slot.value = std::move(value);
            slot.nextFree = NoFreeSlot;
            ++m_count;
            return Handle<T>{index, slot.generation};
        }

        // ハンドルが指す中身を消す。戻り値: 消したら true、すでに無かったら false。
        bool Remove(Handle<T> handle)
        {
            Slot* slot = FindSlot(handle);
            if (slot == nullptr)
            {
                return false;
            }

            slot->value.reset(); // 中身を破棄する(T のデストラクタが呼ばれる)

            // 世代を進める。これで、この場所を指していた古いハンドルは、すべて無効になる。
            // 0 は「無効」用なので、一周して 0 になったら 1 に飛ばす。
            ++slot->generation;
            if (slot->generation == 0)
            {
                slot->generation = 1;
            }

            // 空きリストの先頭につなぐ。
            slot->nextFree = m_firstFree;
            m_firstFree = handle.index;
            --m_count;
            return true;
        }

        // ハンドルが指す中身へのポインタを返す。消されている(古いハンドル)なら nullptr。
        // 注意: 返したポインタは、次に Add を呼ぶと無効になることがある。
        //   vector は大きくなるとき、中身を別のメモリへ引っ越すため。ポインタは長く持たず、毎回 Get で引くこと。
        T* Get(Handle<T> handle)
        {
            Slot* slot = FindSlot(handle);
            return slot != nullptr ? &*slot->value : nullptr; // &*: optional の中身のアドレス
        }

        // const 版。const な HandleTable からも読めるようにする(中身は書き換えられない)。
        const T* Get(Handle<T> handle) const
        {
            const Slot* slot = FindSlot(handle);
            return slot != nullptr ? &*slot->value : nullptr;
        }

        // ハンドルが指す中身が、まだあるか。
        bool Contains(Handle<T> handle) const { return FindSlot(handle) != nullptr; }

        // 今入っている中身の数。
        std::size_t Size() const { return m_count; }

    private:
        static constexpr std::uint32_t NoFreeSlot = 0xFFFFFFFFu; // 空きリストの終わりの印

        struct Slot
        {
            std::optional<T> value;           // 中身。消されていれば空
            std::uint32_t generation = 0;     // このスロットの今の世代
            std::uint32_t nextFree = NoFreeSlot; // 空いているとき、次の空きスロットの番号
        };

        // ハンドルが今も有効なら、そのスロットを返す。無効なら nullptr。
        // 有効の条件: 場所が範囲内で、世代が一致し、中身がある。
        const Slot* FindSlot(Handle<T> handle) const
        {
            if (handle.IsNull() || handle.index >= m_slots.size())
            {
                return nullptr;
            }
            const Slot& slot = m_slots[handle.index];
            if (slot.generation != handle.generation || !slot.value.has_value())
            {
                return nullptr;
            }
            return &slot;
        }

        // 書き換え用。const 版を呼び、結果から const を外す(同じ判定を2回書かないための定番の書き方)。
        Slot* FindSlot(Handle<T> handle)
        {
            return const_cast<Slot*>(static_cast<const HandleTable*>(this)->FindSlot(handle));
        }

        std::vector<Slot> m_slots;
        std::uint32_t m_firstFree = NoFreeSlot; // 空きリストの先頭
        std::size_t m_count = 0;
    };
}
