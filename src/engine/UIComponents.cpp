// UIComponents.h の実装(UI の画像と文字を描く)。
#include "engine/UIComponents.h"

#include "engine/AssetLoader.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h" // ChooseTexture、ApplyRegion
#include "engine/UIActor.h"

#include <cmath> // std::round

namespace husky::engine
{
    UIScreenRect ComputeUIScreenRect(const UIActor& actor)
    {
        // UI は回さないので、ワールドの行列は「拡大 + 移動」だけ。移動の値が pivot の点の画面の位置、拡大が Canvas の倍率。
        const core::Matrix4 world = actor.GetWorldMatrix();
        const float scaleX = world.m[0];
        const float scaleY = world.m[5];
        // Canvas の根は、四角の左下が transform の原点(UILayout の決まり)。ほかは pivot の点が原点。
        const bool isCanvas = actor.GetComponent<CanvasComponent>() != nullptr;
        const float pivotX = isCanvas ? 0.0f : actor.rectTransform.pivot.x;
        const float pivotY = isCanvas ? 0.0f : actor.rectTransform.pivot.y;
        const UIRect& rect = actor.GetRect();
        UIScreenRect result;
        result.width = rect.width * scaleX;
        result.height = rect.height * scaleY;
        result.left = world.m[12] - pivotX * result.width;
        result.bottom = world.m[13] - pivotY * result.height;
        result.scale = scaleY;
        return result;
    }

    float ComputeUIFontPixelSize(float fontSize, float canvasScale)
    {
        const float size = std::round(fontSize * canvasScale);
        return size < 1.0f ? 1.0f : size;
    }

    namespace
    {
        void DrawUIImage(UIImageComponent& image, const UIScreenRect& rect, render::RenderDevice& renderDevice, AssetLoader& loader)
        {
            render::Sprite sprite;
            sprite.model.m[12] = rect.left;
            sprite.model.m[13] = rect.bottom;
            sprite.width = rect.width;
            sprite.height = rect.height;
            sprite.pivotX = 0.0f; // 左下の角を位置に合わせる
            sprite.pivotY = 0.0f;
            sprite.color = image.color;
            PixelRect region;
            const TextureAsset* texture = ChooseTexture(loader, image.sheet, image.sprite, image.texture, image.region, region);
            if (texture != nullptr && !texture->GetTexture().IsNull())
            {
                sprite.texture = texture->GetTexture();
                ApplyRegion(region, texture->GetWidth(), texture->GetHeight(), sprite);
            }
            // 画像が無い・読み込み中なら、テクスチャ無し(白い板に color を掛けた色の板)
            renderDevice.DrawSprite(sprite);
        }

        void DrawUIText(UITextComponent& text, const UIScreenRect& rect, render::RenderDevice& renderDevice, AssetLoader& loader,
                      TextLayoutResult& layout)
        {
            if (text.text.empty())
            {
                return;
            }
            FontAsset* font = loader.Resolve(text.font);
            FontAtlas* atlas = (font != nullptr && font->IsLoaded()) ? font->GetAtlas(ComputeUIFontPixelSize(text.fontSize, rect.scale))
                                                                    : nullptr;
            if (atlas == nullptr)
            {
                return; // フォントを指していない・読み込み中・読めなかった
            }
            // 画面のピクセルで並べる(行の高さも字の幅も、アトラスの大きさ = 字の大きさ × 拡大)
            TextLayoutOptions options;
            options.maxWidth = text.wrap ? rect.width : 0.0f;
            options.align = text.alignment;
            options.lineSpacing = text.lineSpacing;
            LayoutText(*atlas, text.text, options, layout);
            atlas->Upload(renderDevice); // 初めて出た字を GPU に送る

            float left = rect.left;
            if (!text.wrap) // 折り返さないときは、いちばん長い行の幅の中で寄せてあるので、四角の中で寄せ直す
            {
                if (text.alignment == TextAlign::Center)
                {
                    left += (rect.width - layout.width) * 0.5f;
                }
                else if (text.alignment == TextAlign::Right)
                {
                    left += rect.width - layout.width;
                }
            }
            float top = rect.bottom + rect.height;
            if (text.verticalAlignment == UIVerticalAlign::Middle)
            {
                top -= (rect.height - layout.height) * 0.5f;
            }
            else if (text.verticalAlignment == UIVerticalAlign::Bottom)
            {
                top -= rect.height - layout.height;
            }
            DrawTextLayout(renderDevice, *atlas, layout, left, top, text.color);
        }
    }

    void DrawUI(Scene& scene, render::RenderDevice& renderDevice, AssetLoader& loader, TextLayoutResult& layout)
    {
        scene.ForEachActor([&](Actor& actor) {
            if (!actor.IsA<UIActor>() || !actor.IsDrawnOnScreen())
            {
                return;
            }
            UIImageComponent* image = actor.GetComponent<UIImageComponent>();
            UITextComponent* text = actor.GetComponent<UITextComponent>();
            if (image == nullptr && text == nullptr)
            {
                return;
            }
            const UIScreenRect rect = ComputeUIScreenRect(static_cast<const UIActor&>(actor));
            if (image != nullptr)
            {
                DrawUIImage(*image, rect, renderDevice, loader);
            }
            if (text != nullptr)
            {
                DrawUIText(*text, rect, renderDevice, loader, layout);
            }
        });
    }
}
