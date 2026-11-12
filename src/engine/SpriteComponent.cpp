// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    namespace
    {
        // アセットのハンドルから、描画層のテクスチャを引く。読めていないアセットや、もう無いアセットなら、何も指さない。
        render::TextureHandle FindTexture(const AssetLoader& loader, core::ObjectHandle textureAsset)
        {
            const TextureAsset* asset = loader.ResolveTexture(textureAsset);
            return (asset != nullptr && asset->IsLoaded()) ? asset->GetTexture() : render::TextureHandle{};
        }
    }

    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, const AssetLoader& loader,
                     core::ObjectHandle defaultTexture)
    {
        // 既定のテクスチャは、スプライトごとに引き直さず、最初に1回だけ引く。
        const render::TextureHandle fallback = FindTexture(loader, defaultTexture);
        ForEachSprite(scene, [&](render::Sprite& sprite, const SpriteComponent& component) {
            sprite.texture = FindTexture(loader, component.texture);
            if (sprite.texture.IsNull())
            {
                sprite.texture = fallback;
            }
            renderDevice.DrawSprite(sprite);
        });
    }
}
