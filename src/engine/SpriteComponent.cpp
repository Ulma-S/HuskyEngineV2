// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    namespace
    {
        // テクスチャのアセットが読めていて、GPU のテクスチャも持っていれば、それを返す。それ以外は nullptr。
        const TextureAsset* DrawableOrNull(const TextureAsset* asset)
        {
            return (asset != nullptr && asset->IsLoaded() && !asset->GetTexture().IsNull()) ? asset : nullptr;
        }
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

    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, AssetLoader& loader,
                     core::ObjectHandle defaultTexture)
    {
        // 既定のテクスチャは、スプライトごとに引き直さず、最初に1回だけ引く。
        const TextureAsset* fallback = DrawableOrNull(loader.ResolveTexture(defaultTexture));
        ForEachSprite(scene, [&](render::Sprite& sprite, SpriteComponent& component) {
            if (const TextureAsset* texture = DrawableOrNull(loader.Resolve(component.texture)))
            {
                // 自分のテクスチャ: 指定された範囲を貼る。
                sprite.texture = texture->GetTexture();
                ApplyRegion(component.region, texture->GetWidth(), texture->GetHeight(), sprite);
            }
            else if (fallback != nullptr)
            {
                // 既定のテクスチャ: 範囲は自分のテクスチャ用なので使わず、全体を貼る(Sprite の既定の UV のまま)。
                sprite.texture = fallback->GetTexture();
            }
            renderDevice.DrawSprite(sprite);
        });
    }
}
