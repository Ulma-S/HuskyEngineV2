// MoveGizmo.h の実装。
#include "tools/MoveGizmo.h"

#if HUSKY_DEVELOPMENT

#include "engine/TransformMatrix.h"

#include <cmath> // std::fabs、std::round

namespace husky::tools
{
    namespace
    {
        constexpr render::Color XColor = {0.90f, 0.25f, 0.25f, 1.0f};      // x は赤(Unity と同じ)
        constexpr render::Color YColor = {0.35f, 0.85f, 0.35f, 1.0f};      // y は緑
        constexpr render::Color CenterColor = {1.0f, 0.85f, 0.30f, 0.45f}; // 真ん中は薄い黄色
        constexpr render::Color HighlightColor = {1.0f, 0.95f, 0.30f, 1.0f}; // 乗せている・つかんでいる部品

        // value を step の倍数のうち、いちばん近いものに丸める(step が 0 以下なら、そのまま)。
        // 例: step 10 なら 23 → 20、27 → 30、-14 → -10。
        float SnapTo(float value, float step)
        {
            return step > 0.0f ? std::round(value / step) * step : value;
        }

        // 真ん中が (x, y)、大きさ width x height、rotation 度回した白い板を1枚足す。
        // 行列は、アクターと同じ道具(MakeModelMatrix: 移動 × 回転 × 拡大縮小)で作る。
        void AddQuad(std::vector<render::Sprite>& out, float x, float y, float width, float height, float rotation,
                     const render::Color& color)
        {
            engine::Transform transform;
            transform.position = {x, y, 0.0f};
            transform.rotation = rotation;
            render::Sprite quad;
            quad.model = engine::MakeModelMatrix(transform);
            quad.width = width;
            quad.height = height;
            quad.color = color;
            out.push_back(quad);
        }
    }

    GizmoHandle PickGizmoHandle(float originX, float originY, float mouseX, float mouseY)
    {
        // ギズモの真ん中から見たマウスの位置(画面のピクセル)。画面の y は下が + なので、上向きを + に直す
        const float dx = mouseX - originX;
        const float up = originY - mouseY;
        const float halfCenter = GizmoSize::CenterSize * 0.5f;
        if (std::fabs(dx) <= halfCenter && std::fabs(up) <= halfCenter)
        {
            return GizmoHandle::Center;
        }
        const float reach = GizmoSize::ArrowLength + GizmoSize::HeadSize; // 矢印の先まで
        if (dx >= 0.0f && dx <= reach && std::fabs(up) <= GizmoSize::PickWidth)
        {
            return GizmoHandle::X;
        }
        if (up >= 0.0f && up <= reach && std::fabs(dx) <= GizmoSize::PickWidth)
        {
            return GizmoHandle::Y;
        }
        return GizmoHandle::None;
    }

    core::Vector3 ApplyGizmoDrag(GizmoHandle handle, const core::Vector3& from, float deltaX, float deltaY, float snap)
    {
        core::Vector3 to = from;
        if (handle == GizmoHandle::X || handle == GizmoHandle::Center)
        {
            to.x = SnapTo(to.x + deltaX, snap);
        }
        if (handle == GizmoHandle::Y || handle == GizmoHandle::Center)
        {
            to.y = SnapTo(to.y + deltaY, snap);
        }
        return to;
    }

    void BuildGizmoSprites(const engine::CameraView& view, const core::Vector3& origin, GizmoHandle highlighted,
                           std::vector<render::Sprite>& out)
    {
        out.clear();
        // 画面のピクセル → ワールドの単位(1 ピクセル = 1 / zoom 単位)。これで、ズームしても画面での大きさが変わらない
        const float pixel = 1.0f / (view.zoom > 0.0f ? view.zoom : 1.0f);
        const float length = GizmoSize::ArrowLength * pixel;
        const float thickness = GizmoSize::Thickness * pixel;
        const float head = GizmoSize::HeadSize * pixel;
        const float center = GizmoSize::CenterSize * pixel;
        auto colorOf = [highlighted](GizmoHandle handle, const render::Color& normal) {
            return handle == highlighted ? HighlightColor : normal;
        };

        // 真ん中の四角(いちばん奥に描く。矢印の付け根が上に重なる)
        AddQuad(out, origin.x, origin.y, center, center, 0.0f, colorOf(GizmoHandle::Center, CenterColor));
        // x の矢印: 軸(真ん中から右へ length)と、先の菱形(45 度回した四角)
        const render::Color x = colorOf(GizmoHandle::X, XColor);
        AddQuad(out, origin.x + length * 0.5f, origin.y, length, thickness, 0.0f, x);
        AddQuad(out, origin.x + length, origin.y, head, head, 45.0f, x);
        // y の矢印: 軸(真ん中から上へ length)と、先の菱形
        const render::Color y = colorOf(GizmoHandle::Y, YColor);
        AddQuad(out, origin.x, origin.y + length * 0.5f, thickness, length, 0.0f, y);
        AddQuad(out, origin.x, origin.y + length, head, head, 45.0f, y);
    }
}

#endif
