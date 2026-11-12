// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    namespace
    {
        // テクスチャのアセットから、描画層のテクスチャを引く。読めていない、または無いなら、何も指さない。
        render::TextureHandle TextureOf(const TextureAsset* asset)
        {
            return (asset != nullptr && asset->IsLoaded()) ? asset->GetTexture() : render::TextureHandle{};
        }
    }

    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, AssetLoader& loader,
                     core::ObjectHandle defaultTexture)
    {
        // 既定のテクスチャは、スプライトごとに引き直さず、最初に1回だけ引く。
        const render::TextureHandle fallback = TextureOf(loader.ResolveTexture(defaultTexture));
        ForEachSprite(scene, [&](render::Sprite& sprite, SpriteComponent& component) {
            sprite.texture = TextureOf(loader.Resolve(component.texture));
            if (sprite.texture.IsNull())
            {
                sprite.texture = fallback;
            }
            renderDevice.DrawSprite(sprite);
        });
    }
}
