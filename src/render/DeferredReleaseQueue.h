// 「解放を数フレーム遅らせる」ための待ち行列。描画層の中で、GPU のリソースを手放すときに使う。GPU は使わない。
//
// なぜ遅らせるか:
//   CPU は GPU より先を走っている。CPU がフレーム 10 の命令を作っているとき、GPU はまだフレーム 8 や 9 の命令を
//   実行していることがある。そこでテクスチャをすぐ壊すと、GPU が実行中の命令が、壊れたテクスチャを読んでしまう。
//   そこで、「手放す」と言われたものは、いったんこの行列に入れておき、GPU が確実に使い終わった
//   (= 数フレーム経った)ところで、本当に解放する。
//
//   (SDL_GPU の Release も、内部で同じように「GPU が使い終わるまで待ってから解放」してくれる。
//    それでも自分で遅らせるのは、エンジンの決まりとして、どの描画の API の上でも同じ考え方で動くようにするため。
//    今後、バッファなど別の GPU リソースにも同じ行列を使う)
//
// 使い方:
//   DeferredReleaseQueue<SDL_GPUTexture*> queue;
//   queue.Push(texture, currentFrame);                        // 手放す(まだ解放しない)
//   queue.ReleaseReady(currentFrame, [](SDL_GPUTexture* t) {   // 毎フレーム1回。十分に古いものだけを解放する
//       SDL_ReleaseGPUTexture(device, t);
//   });
//
// 動作スレッド: メインスレッド(RenderDevice と同じ)
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility> // std::move
#include <vector>

namespace husky::render
{
    template <typename T>
    class DeferredReleaseQueue final
    {
    public:
        // 手放してから、本当に解放するまでに待つフレームの数。
        // GPU が CPU から遅れるのは、ふつう 2〜3 フレームまで。余裕を持って 3 にしておく。
        static constexpr std::uint64_t FramesToWait = 3;

        // item を手放す。frame: 今のフレームの番号(RenderDevice が数えている、増え続ける番号)。
        void Push(T item, std::uint64_t frame) { m_entries.push_back(Entry{std::move(item), frame}); }

        // 手放してから FramesToWait 以上経ったものについて release(item) を呼び、行列から外す。
        // 毎フレーム1回呼ぶ。戻り値: 解放した数。
        template <typename Function>
        std::size_t ReleaseReady(std::uint64_t currentFrame, Function&& release)
        {
            // 行列から外すものを詰めながら、残すものを前へ寄せる(順番は保つ)。
            std::size_t kept = 0;
            std::size_t released = 0;
            for (std::size_t i = 0; i < m_entries.size(); ++i)
            {
                // 「currentFrame - frame >= FramesToWait」と引き算で書くと、frame の方が大きいとき
                // 符号なし整数(uint64_t)は負にならず、とても大きな数に回り込んで「十分に経った」と誤る。足し算で比べる。
                if (currentFrame >= m_entries[i].frame + FramesToWait)
                {
                    release(m_entries[i].item);
                    ++released;
                }
                else
                {
                    if (kept != i)
                    {
                        m_entries[kept] = std::move(m_entries[i]);
                    }
                    ++kept;
                }
            }
            m_entries.resize(kept);
            return released;
        }

        // 待っているものを、フレームに関係なくすべて解放する(終了のとき。GPU が止まるのを待ってから呼ぶ)。
        template <typename Function>
        void ReleaseAll(Function&& release)
        {
            for (Entry& entry : m_entries)
            {
                release(entry.item);
            }
            m_entries.clear();
        }

        // 解放を待っている数。
        std::size_t Size() const { return m_entries.size(); }

    private:
        struct Entry
        {
            T item;
            std::uint64_t frame = 0; // 手放したフレームの番号
        };
        std::vector<Entry> m_entries;
    };
}
