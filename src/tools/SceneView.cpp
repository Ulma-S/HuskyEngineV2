// SceneView.h の実装。
#include "tools/SceneView.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/SpriteComponent.h"
#include "tools/EditorPlayMode.h"
#include "tools/Inspector.h"
#include "tools/SceneGrid.h"

#include <imgui.h>

#include <algorithm> // std::clamp
#include <cmath>     // std::pow

namespace husky::tools
{
    engine::CameraView ZoomAt(const engine::CameraView& view, float steps, float screenX, float screenY, float screenWidth,
                              float screenHeight)
    {
        // 1. ズームの前に、マウスの下に映っているワールドの位置
        const engine::CameraMatrices before =
            engine::MakeOrthographicCamera(view.centerX, view.centerY, view.zoom, screenWidth, screenHeight);
        const core::Vector3 pointBefore = engine::ScreenToWorld(before, screenX, screenY, screenWidth, screenHeight);

        // 2. 拡大率を変える(1 段ごとに 1.1 倍。かけ算にすると、寄っているときも引いているときも同じ感じで変わる)
        engine::CameraView result = view;
        result.zoom = std::clamp(view.zoom * std::pow(1.1f, steps), 0.05f, 20.0f);

        // 3. ズームの後に同じ画面の点に映るワールドの位置を求め、ずれた分だけ真ん中を戻す
        //    (これで、マウスの下のものは動かずに、そこを中心に大きく・小さくなる)
        const engine::CameraMatrices after =
            engine::MakeOrthographicCamera(result.centerX, result.centerY, result.zoom, screenWidth, screenHeight);
        const core::Vector3 pointAfter = engine::ScreenToWorld(after, screenX, screenY, screenWidth, screenHeight);
        result.centerX += pointBefore.x - pointAfter.x;
        result.centerY += pointBefore.y - pointAfter.y;
        return result;
    }

    void SceneView::Update(engine::Application& app, const EditorPlayMode& playMode, SceneInspector& inspector)
    {
        if (playMode.GetState() != EditorPlayMode::State::Edit)
        {
            app.ClearCameraOverride(); // 再生中はゲームのカメラで映す
            m_gridSprites.clear();     // グリッドも描かない
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const float screenWidth = io.DisplaySize.x;
        const float screenHeight = io.DisplaySize.y;
        if (!m_initialized)
        {
            // ゲームのカメラと同じ見え方から始める(編集を始めた途端に画面が飛ばないように)
            m_view = engine::FindSceneCameraView(app.GetScene(), screenWidth, screenHeight);
            m_initialized = true;
        }

        // マウスが ImGui の窓の上なら、窓の操作を優先する(シーンビューは何もしない)
        if (!io.WantCaptureMouse)
        {
            // パン: 右か中のボタンを押しながら動かすと、つかんだ世界が一緒に動く(カメラは逆へ動く)。
            // マウスの 1 ピクセルは、ワールドの 1 / zoom 単位。画面の y は下が + なので、y は足す向きが逆。
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                m_view.centerX -= io.MouseDelta.x / m_view.zoom;
                m_view.centerY += io.MouseDelta.y / m_view.zoom;
            }
            // ズーム: マウスの下の点を中心に
            if (io.MouseWheel != 0.0f)
            {
                m_view = ZoomAt(m_view, io.MouseWheel, io.MousePos.x, io.MousePos.y, screenWidth, screenHeight);
            }
            // 選ぶ: 左クリックした所の、いちばん手前のスプライトのアクター(無ければ選択を外す)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const engine::CameraMatrices camera =
                    engine::MakeOrthographicCamera(m_view.centerX, m_view.centerY, m_view.zoom, screenWidth, screenHeight);
                const core::Vector3 world = engine::ScreenToWorld(camera, io.MousePos.x, io.MousePos.y, screenWidth, screenHeight);
                engine::Actor* actor = engine::FindSpriteActorAt(app.GetScene(), world.x, world.y);
                inspector.Select(actor != nullptr ? actor->GetHandle() : core::ObjectHandle());
            }
        }
        app.SetCameraOverride(m_view);
        // グリッドの線を、このカメラに合わせて作り直す(次に描くフレームで、同じカメラと一緒に使われる)
        BuildGridSprites(m_view, screenWidth, screenHeight, m_gridSprites);
    }

    void SceneView::DrawGrid(render::RenderDevice& renderDevice) const
    {
        for (const render::Sprite& line : m_gridSprites)
        {
            renderDevice.DrawSprite(line);
        }
    }
}
#endif
