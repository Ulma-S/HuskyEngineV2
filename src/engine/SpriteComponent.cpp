// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

#include "engine/GlmConversion.h"

#include <glm/matrix.hpp> // glm::inverse、glm::determinant

namespace husky::engine
{
    namespace
    {
        // 読み終わっているアセットなら、それを返す。読み込み中、失敗、無いなら nullptr。
        template <typename Asset>
        Asset* LoadedOrNull(Asset* asset)
        {
            return (asset != nullptr && asset->IsLoaded()) ? asset : nullptr;
        }

        // GPU のテクスチャを持っていれば(描けるなら)、それを返す。無ければ nullptr。
        const TextureAsset* DrawableOrNull(const TextureAsset* asset)
        {
            return (asset != nullptr && !asset->GetTexture().IsNull()) ? asset : nullptr;
        }
    }

    Actor* FindSpriteActorAt(World& scene, float worldX, float worldY)
    {
        Actor* found = nullptr;
        ForEachSprite(scene, [&](render::Sprite& sprite, SpriteComponent& component) {
            // 描くときは「ローカル × モデル行列 = ワールド」。逆行列を掛けて、点をスプライトのローカルの座標へ戻す。
            const glm::mat4 model = ToGlm(sprite.model);
            if (glm::determinant(model) == 0.0f)
            {
                return; // 拡大縮小が 0 など、つぶれていて逆行列が無い(点に重なりようがない)
            }
            const glm::vec4 local = glm::inverse(model) * glm::vec4(worldX, worldY, 0.0f, 1.0f);
            // ローカルの四角形(ピボットが原点。ComputeSpriteCorners と同じ決まり)の中か
            const float left = -sprite.width * sprite.pivotX;
            const float bottom = -sprite.height * sprite.pivotY;
            if (local.x >= left && local.x <= left + sprite.width && local.y >= bottom && local.y <= bottom + sprite.height)
            {
                found = component.GetOwner(); // 後に描くもの(手前)で上書きする
            }
        });
        return found;
    }

    void ApplyRegion(const PixelRect& region, int textureWidth, int textureHeight, render::Sprite& sprite)
    {
        if (region.IsEmpty() || textureWidth <= 0 || textureHeight <= 0)
        {
            sprite.u0 = 0.0f;
            sprite.v0 = 0.0f;
            sprite.u1 = 1.0f;
            sprite.v1 = 1.0f;
            return;
        }
        // ピクセルの位置を、画像の幅(高さ)で割ると 0〜1 になる。
        // 例: 64x64 の画像の (32, 0) から 32x32 → u は 0.5〜1.0、v は 0.0〜0.5
        const float width = static_cast<float>(textureWidth);
        const float height = static_cast<float>(textureHeight);
        sprite.u0 = static_cast<float>(region.x) / width;
        sprite.v0 = static_cast<float>(region.y) / height;
        sprite.u1 = static_cast<float>(region.x + region.width) / width;
        sprite.v1 = static_cast<float>(region.y + region.height) / height;
    }

    const TextureAsset* ChooseSpriteTexture(SpriteComponent& component, AssetLoader& loader, PixelRect& outRegion)
    {
        return ChooseTexture(loader, component.sheet, component.sprite, component.texture, component.region, outRegion);
    }

    const TextureAsset* ChooseTexture(AssetLoader& loader, core::AssetRef<SpriteSheetAsset>& sheetReference, std::int32_t sprite,
                                      core::AssetRef<TextureAsset>& textureReference, const PixelRect& region, PixelRect& outRegion)
    {
        outRegion = {};

        // 1. スプライトシートを指していれば、そちらを使う(texture と region より優先)。
        if (!sheetReference.IsNull())
        {
            SpriteSheetAsset* sheet = LoadedOrNull(loader.Resolve(sheetReference));
            if (sheet == nullptr)
            {
                return nullptr; // 読み込み中か失敗
            }
            const TextureAsset* texture = LoadedOrNull(loader.Resolve(sheet->texture));
            if (texture == nullptr)
            {
                return nullptr; // 画像が読み込み中か失敗
            }
            if (const SpriteSlice* slice = sheet->FindSlice(sprite))
            {
                outRegion = slice->rect;
            }
            return texture; // 切り出しが見つからなければ、範囲は空(画像の全体)
        }

        // 2. テクスチャを直接指していれば、その画像と region。
        const TextureAsset* texture = LoadedOrNull(loader.Resolve(textureReference));
        if (texture != nullptr)
        {
            outRegion = region;
        }
        return texture;
    }

    void DrawSprites(World& scene, render::RenderDevice& renderDevice, AssetLoader& loader,
                     core::ObjectHandle defaultTexture, SpritePass pass)
    {
        // 既定のテクスチャは、スプライトごとに引き直さず、最初に1回だけ引く。
        const TextureAsset* fallback = DrawableOrNull(LoadedOrNull(loader.Resolve<TextureAsset>(defaultTexture)));
        ForEachSprite(scene, [&](render::Sprite& sprite, SpriteComponent& component) {
            PixelRect region;
            if (const TextureAsset* texture = DrawableOrNull(ChooseSpriteTexture(component, loader, region)))
            {
                // 自分のテクスチャ: 決まった範囲を貼る。
                sprite.texture = texture->GetTexture();
                ApplyRegion(region, texture->GetWidth(), texture->GetHeight(), sprite);
            }
            else if (fallback != nullptr)
            {
                // 既定のテクスチャ: 範囲は自分のテクスチャ用なので使わず、全体を貼る(Sprite の既定の UV のまま)。
                sprite.texture = fallback->GetTexture();
            }
            renderDevice.DrawSprite(sprite);
        }, pass);
    }
}
