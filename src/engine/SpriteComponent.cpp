// SpriteComponent.h の実装。
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice)
    {
        ForEachSprite(scene, [&](const render::Sprite& sprite) { renderDevice.DrawSprite(sprite); });
    }
}
