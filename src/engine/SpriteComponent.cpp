// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, render::TextureHandle defaultTexture)
    {
        // render::Sprite sprite は、ForEachSprite が渡したものの写し(値渡し)。書き換えても元のコンポーネントは変わらない。
        ForEachSprite(scene, [&](render::Sprite sprite) {
            if (sprite.texture.IsNull())
            {
                sprite.texture = defaultTexture;
            }
            renderDevice.DrawSprite(sprite);
        });
    }
}
