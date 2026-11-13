// SceneView.h の実装。
#include "tools/SceneView.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/SpriteComponent.h"
#include "tools/EditorCommands.h"
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

    bool CommitActorMove(engine::Application& app, EditorCommandRegistry& commands, engine::Actor& actor,
                         const core::Vector3& from)
    {
        // インスペクタの書き換え(PropertyEditRecorder)と同じ形: いったん前の値に戻してから、コマンドで新しい値を書く。
        // こうすると property.set が「前の値」を正しく控え、Undo でつかむ前の位置に戻れる。
        const core::Vector3 to = actor.transform.position;
        actor.transform.position = from;
        engine::ArchiveValue position = engine::ArchiveValue::MakeObject();
        position.Set("x", engine::ArchiveValue(static_cast<double>(to.x)));
        position.Set("y", engine::ArchiveValue(static_cast<double>(to.y)));
        engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
        args.Set("actor", engine::ArchiveValue(actor.guid.ToString()));
        args.Set("property", engine::ArchiveValue("transform.position"));
        args.Set("value", std::move(position));
        if (!commands.Execute("property.set", app, args).succeeded)
        {
            actor.transform.position = to; // コマンドが使えない・失敗した: 動かした所のまま(履歴には残らない)
            return false;
        }
        return true;
    }

    void SceneView::Update(engine::Application& app, const EditorPlayMode& playMode, SceneInspector& inspector,
                           EditorCommandRegistry* commands)
    {
        if (playMode.GetState() != EditorPlayMode::State::Edit)
        {
            app.ClearCameraOverride(); // 再生中はゲームのカメラで映す
            m_gridSprites.clear();     // グリッドも描かない
            m_dragging = core::ObjectHandle();
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

        // マウスの下に映っているワールドの位置(このフレームのエディタのカメラで。パンやズームをした後の値を使う)
        auto MouseWorld = [this](const ImGuiIO& mouse, float width, float height) {
            const engine::CameraMatrices camera = engine::MakeOrthographicCamera(m_view.centerX, m_view.centerY, m_view.zoom, width, height);
            return engine::ScreenToWorld(camera, mouse.MousePos.x, mouse.MousePos.y, width, height);
        };

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
            // 選ぶ・つかむ: 左ボタンを押した所の、いちばん手前のスプライトのアクターを選び、そのままドラッグで動かせるようにする
            // (無い所なら選択を外す)。つかんだ点とアクターの位置のずれを覚えておくと、動かしても飛ばない。
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const core::Vector3 world = MouseWorld(io, screenWidth, screenHeight);
                engine::Actor* actor = engine::FindSpriteActorAt(app.GetScene(), world.x, world.y);
                inspector.Select(actor != nullptr ? actor->GetHandle() : core::ObjectHandle());
                m_dragging = actor != nullptr ? actor->GetHandle() : core::ObjectHandle();
                if (actor != nullptr)
                {
                    m_dragFrom = actor->transform.position;
                    m_dragOffsetX = actor->transform.position.x - world.x;
                    m_dragOffsetY = actor->transform.position.y - world.y;
                }
            }
        }

        // ドラッグ中(窓の上へ出ても続ける。押し始めがシーンビューなら、ImGui は窓の操作にしない)
        if (!m_dragging.IsNull())
        {
            engine::Actor* actor = app.GetRegistry().Resolve<engine::Actor>(m_dragging);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                m_dragging = core::ObjectHandle(); // 動かしている間に消えた
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // 動かしている間は、値をその場で書き換えて見せる(まだ履歴には積まない)
                const core::Vector3 world = MouseWorld(io, screenWidth, screenHeight);
                actor->transform.position.x = world.x + m_dragOffsetX;
                actor->transform.position.y = world.y + m_dragOffsetY;
            }
            else
            {
                // 離した: 動いていれば、ドラッグ全体を1回の property.set にする(Ctrl+Z 1回で、つかむ前の位置へ戻る)
                if (actor->transform.position != m_dragFrom && commands != nullptr)
                {
                    CommitActorMove(app, *commands, *actor, m_dragFrom);
                }
                m_dragging = core::ObjectHandle();
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
