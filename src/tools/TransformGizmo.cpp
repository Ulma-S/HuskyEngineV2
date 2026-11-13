// TransformGizmo.h の実装。
#include "tools/TransformGizmo.h"

#if HUSKY_DEVELOPMENT

#include "engine/TransformMatrix.h"

#include <cmath> // std::fabs、std::round、std::atan2、std::cos、std::sin、std::hypot

namespace husky::tools
{
    namespace
    {
        constexpr float Pi = 3.14159265358979f;
        constexpr render::Color XColor = {0.90f, 0.25f, 0.25f, 1.0f};        // x は赤(Unity と同じ)
        constexpr render::Color YColor = {0.35f, 0.85f, 0.35f, 1.0f};        // y は緑
        constexpr render::Color RingColor = {0.35f, 0.55f, 1.0f, 1.0f};      // 回転の輪(z 軸まわり)は青。Unity の z と同じ色
        constexpr render::Color CenterColor = {1.0f, 0.85f, 0.30f, 0.45f};   // 真ん中は薄い黄色
        constexpr render::Color HighlightColor = {1.0f, 0.95f, 0.30f, 1.0f}; // 乗せている・つかんでいる部品

        // value を step の倍数のうち、いちばん近いものに丸める(step が 0 以下なら、そのまま)。
        // 例: step 10 なら 23 → 20、27 → 30、-14 → -10。
        float SnapTo(float value, float step)
        {
            return step > 0.0f ? std::round(value / step) * step : value;
        }

        // 2 次元のベクトル (x, y) を、反時計回りに degrees 度回す(回転の行列を手で書いたもの。2 次元なので glm を使うまでもない)。
        //   x' = x cos - y sin、y' = x sin + y cos
        void Rotate2D(float x, float y, float degrees, float& outX, float& outY)
        {
            const float radians = degrees * Pi / 180.0f;
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            outX = x * c - y * s;
            outY = x * s + y * c;
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

        // 移動・拡大縮小の形(真ん中の四角と、x・y の軸)の当たり。(dx, up) は、ギズモの真ん中から見たマウスの位置
        // (画面のピクセル。上が +)を、軸の向きに直したもの。
        GizmoHandle PickAxes(float dx, float up)
        {
            const float halfCenter = GizmoSize::CenterSize * 0.5f;
            if (std::fabs(dx) <= halfCenter && std::fabs(up) <= halfCenter)
            {
                return GizmoHandle::Center;
            }
            const float reach = GizmoSize::ArrowLength + GizmoSize::HeadSize; // 軸の先まで
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
    }

    GizmoHandle PickGizmoHandle(GizmoTool tool, float originX, float originY, float mouseX, float mouseY,
                                float rotationDegrees)
    {
        // ギズモの真ん中から見たマウスの位置(画面のピクセル)。画面の y は下が + なので、上向きを + に直す
        const float dx = mouseX - originX;
        const float up = originY - mouseY;
        switch (tool)
        {
        case GizmoTool::Rotate:
            // 輪の線(半径 RingRadius)から PickWidth 以内なら、輪をつかむ
            return std::fabs(std::hypot(dx, up) - GizmoSize::RingRadius) <= GizmoSize::PickWidth ? GizmoHandle::Ring
                                                                                                 : GizmoHandle::None;
        case GizmoTool::Scale:
        {
            // 拡大縮小の軸はアクターと一緒に回っているので、マウスの位置を逆に回して、回っていない軸と比べる
            // (RS-2 のアクターの当たりと同じ考え方: 相手を回す代わりに、点のほうを戻す)
            float localX = 0.0f;
            float localUp = 0.0f;
            Rotate2D(dx, up, -rotationDegrees, localX, localUp);
            return PickAxes(localX, localUp);
        }
        case GizmoTool::Move:
        default:
            return PickAxes(dx, up); // 移動の矢印はワールドの向き
        }
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

    float MouseAngleDelta(float originX, float originY, float fromX, float fromY, float toX, float toY)
    {
        // atan2(上, 右) は、右向きを 0 として反時計回りに測った角度(ラジアン、-π〜π)。画面の y は下が + なので上向きに直す
        const float before = std::atan2(originY - fromY, fromX - originX);
        const float after = std::atan2(originY - toY, toX - originX);
        float delta = (after - before) * 180.0f / Pi;
        // -180〜+180 に収める(-π と π の境目をまたいだとき、359 度ではなく -1 度回ったと考える)
        while (delta > 180.0f)
        {
            delta -= 360.0f;
        }
        while (delta < -180.0f)
        {
            delta += 360.0f;
        }
        return delta;
    }

    float ApplyRotateDrag(float fromDegrees, float deltaDegrees, float snapDegrees)
    {
        return SnapTo(fromDegrees + deltaDegrees, snapDegrees);
    }

    core::Vector3 ApplyScaleDrag(GizmoHandle handle, const core::Vector3& fromScale, float rotationDegrees, float startX,
                                 float startY, float mouseX, float mouseY, float snap)
    {
        // つかんでから動いた量(画面のピクセル。上が +)を、アクターのローカルの向きに直す
        float localX = 0.0f;
        float localUp = 0.0f;
        Rotate2D(mouseX - startX, startY - mouseY, -rotationDegrees, localX, localUp);
        // 軸の長さだけ引っ張ると +1(元の 2 倍)。押し戻すと小さくなり、軸の長さだけ戻すと 0 になる
        const float factorX = 1.0f + localX / GizmoSize::ArrowLength;
        const float factorY = 1.0f + localUp / GizmoSize::ArrowLength;
        const float factorBoth = 1.0f + (localX + localUp) * 0.5f / GizmoSize::ArrowLength; // 右か上へ引っ張ると大きく
        core::Vector3 to = fromScale;
        if (handle == GizmoHandle::X)
        {
            to.x = SnapTo(fromScale.x * factorX, snap);
        }
        else if (handle == GizmoHandle::Y)
        {
            to.y = SnapTo(fromScale.y * factorY, snap);
        }
        else if (handle == GizmoHandle::Center)
        {
            to.x = SnapTo(fromScale.x * factorBoth, snap);
            to.y = SnapTo(fromScale.y * factorBoth, snap);
        }
        return to;
    }

    void BuildGizmoSprites(GizmoTool tool, const engine::CameraView& view, const core::Vector3& origin, float rotationDegrees,
                           GizmoHandle highlighted, std::vector<render::Sprite>& out)
    {
        out.clear();
        // 画面のピクセル → ワールドの単位(1 ピクセル = 1 / zoom 単位)。これで、ズームしても画面での大きさが変わらない
        const float pixel = 1.0f / (view.zoom > 0.0f ? view.zoom : 1.0f);
        const float thickness = GizmoSize::Thickness * pixel;
        auto colorOf = [highlighted](GizmoHandle handle, const render::Color& normal) {
            return handle == highlighted ? HighlightColor : normal;
        };

        if (tool == GizmoTool::Rotate)
        {
            // 輪: 短い線を円に沿って並べる(線の向きは円の接線 = 半径の向き + 90 度)
            const float radius = GizmoSize::RingRadius * pixel;
            const float segment = 2.0f * Pi * radius / static_cast<float>(GizmoSize::RingSegments) * 1.1f; // 少し重ねて隙間を消す
            const render::Color color = colorOf(GizmoHandle::Ring, RingColor);
            for (int i = 0; i < GizmoSize::RingSegments; ++i)
            {
                const float angle = 2.0f * Pi * (static_cast<float>(i) + 0.5f) / static_cast<float>(GizmoSize::RingSegments);
                AddQuad(out, origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle), segment, thickness,
                        angle * 180.0f / Pi + 90.0f, color);
            }
            // 今の向きの目印: 真ん中から、アクターの右向き(ローカルの +x)へ短い線
            float dirX = 0.0f;
            float dirY = 0.0f;
            Rotate2D(radius * 0.5f, 0.0f, rotationDegrees, dirX, dirY);
            AddQuad(out, origin.x + dirX, origin.y + dirY, radius, thickness, rotationDegrees, color);
            return;
        }

        // 移動と拡大縮小: 真ん中の四角と、x・y の軸。移動はワールドの向き、拡大縮小はアクターと一緒に回す
        const float axisRotation = tool == GizmoTool::Scale ? rotationDegrees : 0.0f;
        const float length = GizmoSize::ArrowLength * pixel;
        const float head = GizmoSize::HeadSize * pixel;
        const float center = GizmoSize::CenterSize * pixel;
        // 先の形: 移動は菱形(45 度回した四角)、拡大縮小は四角(Unity と同じ見分け方)
        const float headRotation = axisRotation + (tool == GizmoTool::Move ? 45.0f : 0.0f);

        // 真ん中の四角(いちばん奥に描く。軸の付け根が上に重なる)
        AddQuad(out, origin.x, origin.y, center, center, axisRotation, colorOf(GizmoHandle::Center, CenterColor));
        // 軸1本: ローカルの向き (dirX, dirY) へ、真ん中から length の線と、その先
        auto addAxis = [&](float dirX, float dirY, float lineWidth, float lineHeight, const render::Color& color) {
            float x = 0.0f;
            float y = 0.0f;
            Rotate2D(dirX * length * 0.5f, dirY * length * 0.5f, axisRotation, x, y);
            AddQuad(out, origin.x + x, origin.y + y, lineWidth, lineHeight, axisRotation, color);
            Rotate2D(dirX * length, dirY * length, axisRotation, x, y);
            AddQuad(out, origin.x + x, origin.y + y, head, head, headRotation, color);
        };
        addAxis(1.0f, 0.0f, length, thickness, colorOf(GizmoHandle::X, XColor));
        addAxis(0.0f, 1.0f, thickness, length, colorOf(GizmoHandle::Y, YColor));
    }

    const char* GetGizmoToolName(GizmoTool tool)
    {
        switch (tool)
        {
        case GizmoTool::Move: return "Move (W)";
        case GizmoTool::Rotate: return "Rotate (E)";
        case GizmoTool::Scale: return "Scale (R)";
        }
        return "?";
    }
}

#endif
