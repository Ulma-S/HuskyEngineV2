// スプライトを「同じテクスチャと同じ行列が続く区間」ごとにまとめる(バッチ)。描画層の中で使う。GPU は使わない。
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
// 行列(カメラ。RenderDevice::SetViewProjection)もフレームの途中で替えられる(UI-2a。シーンはワールドのカメラ、
// UI は画面の座標)。行列も描画命令の前に GPU に渡すものなので、行列が変わったところでも区間を切る。
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
        std::uint32_t matrixIndex = 0; // この区間を、このフレームの何番目の行列で描くか(UI-2a)
    };

    // スプライトを1枚積んだことを、batches に反映する。
    // 最後の区間と同じテクスチャで同じ行列なら、その区間を1枚伸ばす。どちらかが違えば、新しい区間を始める。
    // spriteIndex: 積んだスプライトが何番目か(0 から。呼ぶたびに 1 ずつ増やす)。
    // matrixIndex: このスプライトを描く行列が、このフレームの何番目か(行列を替えなければ 0 のまま)。
    inline void AddSpriteToBatches(std::vector<SpriteBatch>& batches, TextureHandle texture, std::uint32_t spriteIndex,
                                   std::uint32_t matrixIndex = 0)
    {
        // empty() は「中身が空か」。back() は最後の要素(C# の list[list.Count - 1])。
        if (!batches.empty() && batches.back().texture == texture && batches.back().matrixIndex == matrixIndex)
        {
            ++batches.back().spriteCount;
            return;
        }
        batches.push_back(SpriteBatch{texture, spriteIndex, 1, matrixIndex});
    }
}
