// 当たり判定と物理で、2D と 3D が共有する型(設計メモ「当たり判定と物理」: 2D と 3D は無理に合わせず、
// どちらでも同じ意味になるものだけを共有する)。今は 2D(PhysicsWorld2D.h)だけが使う。
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include "core/Object.h" // core::ObjectHandle

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

    // 重なりの始まり・終わりのイベント(COL-3。2D と 3D で同じ形にする)。
    // 重なった2つのアクターの両方に、それぞれ自分の側から見た形で届く(Actor::OnOverlap と、そのアクターの全コンポーネントの
    // Component::OnOverlap)。相手はハンドルで渡す(生ポインタを外に出さない。CLAUDE.md「リフレクション」)。
    // トリガーかどうかに関係なく届く(今は当たり判定だけなので、どれも「重なり」。物理を足したら、押し合うものの接触も別に知らせる)。
    struct OverlapEvent
    {
        bool begin = true;                // true: 重なり始めた、false: 離れた
        core::ObjectHandle selfCollider;  // 自分の側のコライダー(コンポーネント)
        core::ObjectHandle otherCollider; // 相手のコライダー
        core::ObjectHandle otherActor;    // 相手のアクター(相手が消えた後の「離れた」では、もう無いことがある)
    };
}
