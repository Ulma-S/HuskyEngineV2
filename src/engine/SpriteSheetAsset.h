// スプライトシートのアセット: 1 枚の画像(テクスチャ)を「どう切り出して使うか」を書いた、自分で作るデータ。GC 管理のオブジェクト。
//
// ファイル(.spritesheet、中身は JSON)。画像は GUID で指す:
//   assets/sprites/shapes.spritesheet
//   {
//     "texture": "79c3b0af-c595-475c-96a9-6925bdf98dd7",
//     "sprites": [
//       { "id": 1917386538, "name": "diamond", "x": 32, "y": 32, "width": 32, "height": 32 }
//     ]
//   }
// 切り出しの書き方と ID の決まりは SpriteSlices.h を参照。ID の無い切り出しは、開発ビルドが起動時に振る。
//
// テクスチャとの関係:
//   - 画像の .meta は「元の画像をどう取り込むか」(インポートの設定)。ここは「取り込んだ画像を、ゲームでどう使うか」。
//   - 1 枚の画像を、テクスチャとして直接使いながら、スプライトシートとしても使える。スプライトシートは何個作ってもよい。
//     どれも同じ GUID のテクスチャを指すので、ローダは画像を1回だけ読み、GPU のテクスチャも1枚だけ。
//   - texture はプロパティ(AssetRef。強い参照)なので、このアセットが生きている間は、画像も GC に回収されない。
//
// 作るのは AssetLoader だけ(AssetLoader::LoadSpriteSheet)。読み終わると、ローダが中の画像の読み込みも始める(依存の読み込み)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/Object.h"
#include "engine/AssetState.h"
#include "engine/SpriteSlices.h"
#include "engine/TextureAsset.h"

#include <cstdint>
#include <vector>

namespace husky::engine
{
    class AssetLoader;

    class SpriteSheetAsset : public core::Object
    {
        HUSKY_OBJECT(SpriteSheetAsset, core::Object)
    public:
        // 回収されるとき、ローダのキャッシュから外してもらう。
        ~SpriteSheetAsset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }

        // 切り出しの一覧。読み終わる(IsLoaded)までは空。
        const std::vector<SpriteSlice>& GetSlices() const { return m_slices; }

        // ID で切り出しを探す。無ければ nullptr(切り出しは多くても数十なので、先頭から順に探す)。
        const SpriteSlice* FindSlice(std::int32_t id) const
        {
            for (const SpriteSlice& slice : m_slices)
            {
                if (slice.id == id)
                {
                    return &slice;
                }
            }
            return nullptr;
        }

        // 切り出す元の画像。読み終わるとローダが GUID を書き、読み込みも始める。
        // 書き換えるのはローダだけ(public なのは、プロパティとして登録して GC にたどらせるため)。
        core::AssetRef<TextureAsset> texture;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(texture) // 強い参照: このアセットが生きている間、画像も回収されない
        HUSKY_REFLECTION_END()

    private:
        friend class AssetLoader; // 中身を書くのは AssetLoader だけ

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        std::vector<SpriteSlice> m_slices;
        AssetLoader* m_loader = nullptr; // 作ったローダ(借りているだけ)
    };
}
