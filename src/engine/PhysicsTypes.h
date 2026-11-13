// 当たり判定と物理で、2D と 3D が共有する型(設計メモ「当たり判定と物理」: 2D と 3D は無理に合わせず、
// どちらでも同じ意味になるものだけを共有する)。今は 2D(PhysicsWorld2D.h)だけが使う。
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include <cstdint>

namespace husky::engine
{
    // すべてのレイヤー(32 ビット全部が 1)。
    constexpr std::uint32_t AllCollisionLayers = 0xFFFFFFFFu;

    // レイヤーとマスク: どのグループに属し、どのグループと当たるか(Unity のレイヤーと衝突マトリクスにあたる)。
    //   layer: 自分の属するグループの印(ふつうはビット1つ。1 << 0 〜 1 << 31 の 32 個)
    //   mask:  当たる相手のグループの印(複数のビット)
    // 2つの形 A と B が当たるのは、(A.layer & B.mask) != 0 かつ (B.layer & A.mask) != 0 のとき(お互いに相手を選んでいる)。
    struct CollisionFilter
    {
        std::uint32_t layer = 1u;               // 既定はグループ 0
        std::uint32_t mask = AllCollisionLayers; // 既定はすべてと当たる
    };
}
