// UIRectOverlay.h の実装。
#include "tools/UIRectOverlay.h"

#if HUSKY_DEVELOPMENT

#include "engine/Camera.h"
#include "engine/UIActor.h"
#include "tools/TransformGizmo.h" // AppendQuadOutline(四隅を結ぶ線)

namespace husky::tools
{
    namespace
    {
        // UIActor の四角の四隅(画面のピクセル)。ワールドの行列が画面のピクセルになっているので、
        // 「pivot を原点にした四角 × ワールドの行列」で求まる(スプライトの四隅と同じ計算)。
        render::SpriteCorners ComputeUIRectCorners(const engine::UIActor& actor)
        {
            render::Sprite shape;
            shape.model = actor.GetWorldMatrix();
            shape.width = actor.GetRect().width;
            shape.height = actor.GetRect().height;
            // Canvas の根は、四角の左下が transform の原点(UILayout の決まり)。ほかは pivot の点が原点。
            const bool isCanvas = actor.GetComponent<engine::CanvasComponent>() != nullptr;
            shape.pivotX = isCanvas ? 0.0f : actor.rectTransform.pivot.x;
            shape.pivotY = isCanvas ? 0.0f : actor.rectTransform.pivot.y;
            return render::ComputeSpriteCorners(shape);
        }
    }

    void BuildUIRectOutlines(engine::World& scene, core::ObjectHandle selected, std::vector<render::Sprite>& out)
    {
        out.clear();
        constexpr render::Color LineColor = {0.75f, 0.75f, 0.75f, 0.5f};    // ほかの UI: 薄い灰色
        constexpr render::Color SelectedColor = {1.0f, 0.55f, 0.1f, 1.0f}; // 選んでいる UI: 選択の枠と同じ橙色
        const engine::CameraView pixels{0.0f, 0.0f, 1.0f};                  // 1 単位 = 1 ピクセル(線の太さを画面で一定に)
        const engine::UIActor* selectedUi = nullptr;
        scene.ForEachActor([&](engine::Actor& actor) {
            if (!actor.IsA<engine::UIActor>() || !actor.IsDrawnOnScreen())
            {
                return;
            }
            const engine::UIActor& ui = static_cast<const engine::UIActor&>(actor);
            if (actor.GetHandle() == selected)
            {
                selectedUi = &ui; // 選んでいるものは最後に(手前に)描く
                return;
            }
            AppendQuadOutline(ComputeUIRectCorners(ui), pixels, LineColor, out);
        });
        if (selectedUi != nullptr)
        {
            AppendQuadOutline(ComputeUIRectCorners(*selectedUi), pixels, SelectedColor, out);
        }
    }

    void UIRectOverlay::Draw(engine::World& scene, core::ObjectHandle selected, bool editing, render::RenderDevice& renderDevice)
    {
        if (!editing)
        {
            return;
        }
        BuildUIRectOutlines(scene, selected, m_lines);
        for (const render::Sprite& line : m_lines)
        {
            renderDevice.DrawSprite(line);
        }
    }
}

#endif
