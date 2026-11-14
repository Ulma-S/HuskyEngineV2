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
// 切り出しとアニメーションの書き方、ID の決まりは SpriteSheetData.h を参照。ID は開発ビルドが起動時に振る。
//
// テクスチャとの関係:
//   - 画像の .meta は「元の画像をどう取り込むか」(インポートの設定)。ここは「取り込んだ画像を、ゲームでどう使うか」。
//   - 1 枚の画像を、テクスチャとして直接使いながら、スプライトシートとしても使える。スプライトシートは何個作ってもよい。
//     どれも同じ GUID のテクスチャを指すので、ローダは画像を1回だけ読み、GPU のテクスチャも1枚だけ。
//   - texture はプロパティ(AssetRef。強い参照)なので、このアセットが生きている間は、画像も GC に回収されない。
//
// 作るのは読み方(SpriteSheetAssetKind)だけ。読むのは loader.Load<SpriteSheetAsset>(guid) か Resolve(AssetRef)。読み終わると、ローダが中の画像の読み込みも始める(依存の読み込み)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/AssetRef.h"
#include "engine/Asset.h"
#include "engine/SpriteSheetData.h"
#include "engine/TextureAsset.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace husky::engine
{
    class SpriteSheetAssetKind;

    class SpriteSheetAsset : public Asset
    {
        HUSKY_OBJECT(SpriteSheetAsset, Asset)
    public:
        // 回収されるとき、ローダのキャッシュから外れる(Asset のデストラクタ)。
        ~SpriteSheetAsset() override;

        // 切り出しの一覧。読み終わる(IsLoaded)までは空。
        const std::vector<SpriteSlice>& GetSlices() const { return m_slices; }

        // アニメーションの一覧。読み終わる(IsLoaded)までは空。
        const std::vector<SpriteAnimation>& GetAnimations() const { return m_animations; }

        // ID でアニメーションを探す。無ければ nullptr。
        const SpriteAnimation* FindAnimation(std::int32_t id) const
        {
            for (const SpriteAnimation& animation : m_animations)
            {
                if (animation.id == id)
                {
                    return &animation;
                }
            }
            return nullptr;
        }

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
        friend class SpriteSheetAssetKind; // 中身を書くのは読み方(SpriteSheetAsset.cpp)だけ

        std::vector<SpriteSlice> m_slices;
        std::vector<SpriteAnimation> m_animations;
    };

    // スプライトシート(.spritesheet)の読み方を作る。ローダに RegisterKind で登録する(AL-2)。
    std::unique_ptr<AssetKind> CreateSpriteSheetAssetKind();
}
