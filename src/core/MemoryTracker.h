// メモリの追跡: エンジンが握る「汎用のヒープの入口」と、その使用量の集計。
// 設計メモの「メモリの確保の方針」のアロケータ 2(汎用のヒープ)にあたる。
//
// プログラム全体の operator new / delete(GlobalOperatorNew.cpp で差し替える)は、すべて
// TrackedAllocate / TrackedFree を通る。ここで量と件数を数えてから、実際の確保は標準の malloc に任せる。
// 実体が malloc なので、ASan もそのまま見張れる。
//
// 使い方:
//   const MemoryStats stats = GetMemoryStats();
//   std::printf("確保中: %zu バイト(%zu 件)\n", stats.currentBytes, stats.currentCount);
//
// 動作スレッド: どこでもよい(数える値は std::atomic なので、複数のスレッドから同時に呼んでも壊れない)。
#pragma once

#include "core/AddressSanitizer.h" // HUSKY_NO_SANITIZE_ADDRESS

#include <cstddef>
#include <cstdint>

namespace husky::core
{
    // ある瞬間の、汎用のヒープの使用量。
    struct MemoryStats
    {
        std::size_t currentBytes = 0;      // 今、確保されている量(頼まれた大きさの合計。管理用の余分は含まない)
        std::size_t currentCount = 0;      // 今、確保されている件数
        std::size_t peakBytes = 0;         // currentBytes の、起動してからの最大値
        std::uint64_t totalAllocations = 0; // 起動してからの確保の回数(返したものも含む。毎フレームの確保の多さを見るのに使う)
    };

    // 今の使用量を返す。
    MemoryStats GetMemoryStats();

    // size バイトを、alignment(2 の累乗)の倍数のアドレスで確保し、数える。足りなければ nullptr。
    // size が 0 でも、ほかと重ならない有効なアドレスを返す(C++ の new の決まり)。
    // 普段は直接呼ばない(new / make_unique / コンテナが、差し替えた operator new 経由で呼ぶ)。
    // HUSKY_NO_SANITIZE_ADDRESS: 中で、ASan の印を付けた管理用の場所を読み書きするため、ASan の確認から外す。
    HUSKY_NO_SANITIZE_ADDRESS void* TrackedAllocate(std::size_t size, std::size_t alignment);

    // TrackedAllocate で確保したものを返し、数を減らす。nullptr なら何もしない。
    HUSKY_NO_SANITIZE_ADDRESS void TrackedFree(void* pointer);
}
