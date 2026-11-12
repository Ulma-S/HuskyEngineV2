// スプライトを「同じテクスチャが続く区間」ごとにまとめる(バッチ)。描画層の中で使う。GPU は使わない。
//
// GPU に「このテクスチャで描いて」と伝えるのは、描画命令(draw call)ごとに1回だけ。
// そのため、テクスチャが違うスプライトは、同じ1回の描画命令では描けない。
// 積まれた順(= 奥から手前の順)は変えずに、同じテクスチャが続く間だけを1回の描画命令にまとめる。
//
//   積んだ順:   A A A B B A
//   バッチ:     [A x3] [B x2] [A x1]   → 描画命令 3 回
//
// 順番を変えれば A を1つにまとめられるが、重なったスプライトの前後が入れ替わってしまうので、ここではしない。
// (テクスチャを1枚の大きな画像にまとめる「アトラス」にすれば、テクスチャの切り替えそのものを減らせる。必要になったら検討する)
//
// 動作スレッド: メインスレッド(RenderDevice と同じ)
#pragma once

#include "render/RenderDevice.h"

#include <cstdint>
#include <vector>

namespace husky::render
{
    // 1回の描画命令で描く、スプライトの区間。
    struct SpriteBatch
    {
        TextureHandle texture;         // この区間のスプライトに貼るテクスチャ
        std::uint32_t firstSprite = 0; // 区間の最初のスプライトが、積んだ中で何番目か
        std::uint32_t spriteCount = 0; // 区間のスプライトの数
    };

    // スプライトを1枚積んだことを、batches に反映する。
    // 最後の区間と同じテクスチャなら、その区間を1枚伸ばす。違えば、新しい区間を始める。
    // spriteIndex: 積んだスプライトが何番目か(0 から。呼ぶたびに 1 ずつ増やす)。
    inline void AddSpriteToBatches(std::vector<SpriteBatch>& batches, TextureHandle texture, std::uint32_t spriteIndex)
    {
        // empty() は「中身が空か」。back() は最後の要素(C# の list[list.Count - 1])。
        if (!batches.empty() && batches.back().texture == texture)
        {
            ++batches.back().spriteCount;
            return;
        }
        batches.push_back(SpriteBatch{texture, spriteIndex, 1});
    }
}
