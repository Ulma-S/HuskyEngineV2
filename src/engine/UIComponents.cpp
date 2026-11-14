// UIComponents.h の実装(UI の画像と文字を描く)。
#include "engine/UIComponents.h"

#include "core/Method.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h" // ChooseTexture、ApplyRegion
#include "engine/UIActor.h"

#include <cmath> // std::round
#include <cstring> // std::strcmp
#include <vector>

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
            // 同じアクターにボタンがあれば、状態の色を掛ける(値は書き換えない。UI-6b)
            if (const UIButtonComponent* button = image.GetOwner() != nullptr ? image.GetOwner()->GetComponent<UIButtonComponent>() : nullptr)
            {
                const render::Color& tint = button->GetTintColor();
                sprite.color = {sprite.color.r * tint.r, sprite.color.g * tint.g, sprite.color.b * tint.b, sprite.color.a * tint.a};
            }
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

    // ================= ボタン(UI-6b) =================

    const render::Color& UIButtonComponent::GetTintColor() const
    {
        if (!interactable)
        {
            return disabledColor;
        }
        switch (m_state)
        {
        case UIButtonState::Hovered:
            return hoveredColor;
        case UIButtonState::Pressed:
            return pressedColor;
        case UIButtonState::Normal:
        default:
            return normalColor;
        }
    }

    namespace
    {
        // actor から親へたどり、最初に UIButtonComponent を持つアクターを返す(ボタンの中の文字や画像を押しても、
        // ボタンが押されたことにする。Unity と同じ)。無ければ nullptr。
        Actor* FindButtonActor(Actor* actor)
        {
            for (int depth = 0; actor != nullptr && depth < Actor::MaxHierarchyDepth; ++depth)
            {
                if (actor->GetComponent<UIButtonComponent>() != nullptr)
                {
                    return actor;
                }
                if (actor->GetComponent<CanvasComponent>() != nullptr)
                {
                    return nullptr; // Canvas より上はたどらない
                }
                actor = actor->GetParent();
            }
            return nullptr;
        }
    }

    void ClickUIButton(Actor& buttonActor, UIButtonComponent& button)
    {
        // 1. コードで受ける形: アクターと全コンポーネントの OnUIClick
        UIClickEvent event;
        event.buttonActor = buttonActor.GetHandle();
        buttonActor.OnUIClick(event);
        core::ObjectRegistry* registry = buttonActor.GetOwnerRegistry();
        if (registry == nullptr)
        {
            return;
        }
        for (std::size_t i = 0; i < buttonActor.components.size(); ++i) // 受け取った側が足しても崩れないように添字で回す
        {
            Component* component = registry->Resolve<Component>(buttonActor.components[i]);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                component->OnUIClick(event);
            }
        }

        // 2. インスペクタで選んだ呼び先: target(+ targetComponent)の、引数の無いメソッドをリフレクションで呼ぶ
        if (button.method.empty())
        {
            return;
        }
        Actor* target = button.target.Resolve(*registry);
        if (target == nullptr || target->IsPendingDestroy())
        {
            HUSKY_LOG(SceneLog::UIButtonTargetMissing, buttonActor.name, "the target actor is not set or was destroyed");
            return;
        }
        core::Object* receiver = target;
        if (!button.targetComponent.empty())
        {
            receiver = nullptr;
            for (const core::ObjectHandle handle : target->components)
            {
                Component* component = registry->Resolve<Component>(handle);
                if (component != nullptr && !component->IsPendingDestroy() &&
                    std::strcmp(component->GetType().GetName(), button.targetComponent.c_str()) == 0)
                {
                    receiver = component;
                    break;
                }
            }
            if (receiver == nullptr)
            {
                HUSKY_LOG(SceneLog::UIButtonTargetMissing, buttonActor.name, "the target actor has no such component");
                return;
            }
        }
        const core::Method* method = receiver->GetType().FindMethod(button.method.c_str());
        if (method == nullptr || !method->GetParameterTypes().empty())
        {
            HUSKY_LOG(SceneLog::UIButtonTargetMissing, buttonActor.name,
                      method == nullptr ? "no such method (register it with HUSKY_METHOD)" : "the method takes arguments");
            return;
        }
        const std::vector<core::Value> noArguments;
        core::Value result;
        method->Invoke(*receiver, noArguments, result);
    }

    void UIEventSystem::Update(Scene& scene, Input& input)
    {
        // 1. マウスの下のいちばん手前の当たり(一覧で後ろほど手前に描かれる)。マウスの位置は下が + なので、上が + に直す
        const core::Vector2 mouse = input.GetMousePosition();
        const float x = mouse.x;
        const float y = input.GetScreenHeight() - mouse.y;
        Actor* hovered = nullptr;
        scene.ForEachActor([&](Actor& actor) {
            if (!actor.IsA<UIActor>() || !actor.IsDrawnOnScreen() ||
                (actor.GetComponent<UIImageComponent>() == nullptr && actor.GetComponent<UIButtonComponent>() == nullptr))
            {
                return;
            }
            const UIScreenRect rect = ComputeUIScreenRect(static_cast<const UIActor&>(actor));
            if (x >= rect.left && x < rect.left + rect.width && y >= rect.bottom && y < rect.bottom + rect.height)
            {
                hovered = &actor;
            }
        });
        Actor* hoveredButton = FindButtonActor(hovered);
        m_hovered = hovered != nullptr ? hovered->GetHandle() : core::ObjectHandle{};

        // 2. 押す・押している・離す。UI の上で押したマウスと、ボタンを押している間のマウスは、ゲームに渡さない
        //    (ConsumeMouse は押した・押しているを隠すので、先に読んでおく)
        const bool pressedNow = input.WasMousePressed(MouseButton::Left);
        const bool downNow = input.IsMouseDown(MouseButton::Left);
        const bool releasedNow = input.WasMouseReleased(MouseButton::Left);
        if (pressedNow && hovered != nullptr)
        {
            input.ConsumeMouse();
            const UIButtonComponent* button = hoveredButton != nullptr ? hoveredButton->GetComponent<UIButtonComponent>() : nullptr;
            m_pressed = (button != nullptr && button->interactable) ? hoveredButton->GetHandle() : core::ObjectHandle{};
        }
        else if (downNow && !m_pressed.IsNull())
        {
            input.ConsumeMouse();
        }
        core::ObjectRegistry* registry = scene.GetObjectRegistry();
        Actor* pressed = (!m_pressed.IsNull() && registry != nullptr) ? registry->Resolve<Actor>(m_pressed) : nullptr;
        if (pressed != nullptr && pressed->IsPendingDestroy())
        {
            pressed = nullptr;
        }
        Actor* clicked = nullptr;
        if (releasedNow || !downNow) // 離した(か、離したことを取りこぼした)
        {
            if (releasedNow && pressed != nullptr && pressed == hoveredButton)
            {
                clicked = pressed; // 押したボタンの上で離した
            }
            m_pressed = {};
            pressed = nullptr;
        }

        // 3. ボタンの見た目の状態
        scene.ForEachActor([&](Actor& actor) {
            UIButtonComponent* button = actor.GetComponent<UIButtonComponent>();
            if (button == nullptr)
            {
                return;
            }
            UIButtonState state = UIButtonState::Normal;
            if (button->interactable)
            {
                if (pressed == &actor)
                {
                    state = hoveredButton == &actor ? UIButtonState::Pressed : UIButtonState::Normal;
                }
                else if (pressed == nullptr && hoveredButton == &actor)
                {
                    state = UIButtonState::Hovered;
                }
            }
            button->m_state = state;
        });

        // 4. 押されたボタンを呼ぶ(状態を決めた後。呼ばれた先がシーンを変えてもよいように、最後に)
        if (clicked != nullptr)
        {
            if (UIButtonComponent* button = clicked->GetComponent<UIButtonComponent>())
            {
                ClickUIButton(*clicked, *button);
            }
        }
    }

    void UIEventSystem::Reset(Scene& scene)
    {
        m_hovered = {};
        m_pressed = {};
        scene.ForEachActor([](Actor& actor) {
            if (UIButtonComponent* button = actor.GetComponent<UIButtonComponent>())
            {
                button->m_state = UIButtonState::Normal;
            }
        });
    }
}
