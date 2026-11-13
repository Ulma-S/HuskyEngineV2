// SceneView.h の実装。
#include "tools/SceneView.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/SpriteComponent.h"
#include "tools/EditorCommands.h"
#include "tools/EditorPlayMode.h"
#include "tools/Inspector.h"
#include "tools/MoveGizmo.h"
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
            m_gizmoSprites.clear();
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

        // マウスの下に映っているワールドの位置と、ワールドの点が映る画面の位置(このフレームのエディタのカメラで。パンやズームの後)
        auto MouseWorld = [this](const ImGuiIO& mouse, float width, float height) {
            const engine::CameraMatrices camera = engine::MakeOrthographicCamera(m_view.centerX, m_view.centerY, m_view.zoom, width, height);
            return engine::ScreenToWorld(camera, mouse.MousePos.x, mouse.MousePos.y, width, height);
        };
        // 選んでいるアクター(インスペクタと同じもの)。ギズモはこのアクターの位置に出す
        engine::Actor* selected = app.GetRegistry().Resolve<engine::Actor>(inspector.GetSelected());
        if (selected != nullptr && selected->IsPendingDestroy())
        {
            selected = nullptr;
        }
        // マウスがギズモのどの部品の上か(ドラッグしていない間だけ調べる。画面のピクセルで比べる)
        auto PickAtMouse = [&]() {
            if (selected == nullptr)
            {
                return GizmoHandle::None;
            }
            const engine::CameraMatrices camera =
                engine::MakeOrthographicCamera(m_view.centerX, m_view.centerY, m_view.zoom, screenWidth, screenHeight);
            const core::Vector3 origin = engine::WorldToScreen(camera, selected->transform.position.x,
                                                               selected->transform.position.y, screenWidth, screenHeight);
            return PickGizmoHandle(origin.x, origin.y, io.MousePos.x, io.MousePos.y);
        };

        // マウスが ImGui の窓の上なら、窓の操作を優先する(シーンビューは何もしない)
        m_hovered = GizmoHandle::None;
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
            if (m_dragging.IsNull())
            {
                m_hovered = PickAtMouse(); // 乗せている部品を光らせる
            }
            // 左ボタンを押した: ギズモの上ならつかむ(動かす)。そうでなければ、その下のアクターを選ぶだけ(Unity と同じく、
            // アクターの本体のドラッグでは動かさない。うっかり動かさないため)。何も無い所なら選択を外す。
            // ギズモを先に調べるので、ギズモがほかのアクターに重なっていても、ギズモが優先される。
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const core::Vector3 world = MouseWorld(io, screenWidth, screenHeight);
                const GizmoHandle handle = PickAtMouse();
                if (handle != GizmoHandle::None)
                {
                    m_dragging = selected->GetHandle();
                    m_dragHandle = handle;
                    m_dragFrom = selected->transform.position;
                    m_dragStartX = world.x;
                    m_dragStartY = world.y;
                }
                else
                {
                    engine::Actor* actor = engine::FindSpriteActorAt(app.GetScene(), world.x, world.y);
                    inspector.Select(actor != nullptr ? actor->GetHandle() : core::ObjectHandle());
                    selected = actor;
                }
            }
        }

        // ギズモのドラッグ中(窓の上へ出ても続ける。押し始めがシーンビューなら、ImGui は窓の操作にしない)
        if (!m_dragging.IsNull())
        {
            engine::Actor* actor = app.GetRegistry().Resolve<engine::Actor>(m_dragging);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                m_dragging = core::ObjectHandle(); // 動かしている間に消えた
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // 動かしている間は、値をその場で書き換えて見せる(まだ履歴には積まない)。
                // つかんでからマウスが動いた量を、つかんだ部品の軸だけに足す(X なら x だけ、真ん中なら両方)
                const core::Vector3 world = MouseWorld(io, screenWidth, screenHeight);
                actor->transform.position = ApplyGizmoDrag(m_dragHandle, m_dragFrom, world.x - m_dragStartX, world.y - m_dragStartY);
                m_hovered = m_dragHandle; // つかんでいる部品は光らせたまま
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
        // 移動のギズモ: 選んでいるアクターの位置に(動かした後の位置に作る)
        m_gizmoSprites.clear();
        if (selected != nullptr)
        {
            BuildGizmoSprites(m_view, selected->transform.position, m_hovered, m_gizmoSprites);
        }
    }

    void SceneView::DrawGrid(render::RenderDevice& renderDevice) const
    {
        for (const render::Sprite& line : m_gridSprites)
        {
            renderDevice.DrawSprite(line);
        }
    }

    void SceneView::DrawGizmo(render::RenderDevice& renderDevice) const
    {
        for (const render::Sprite& quad : m_gizmoSprites)
        {
            renderDevice.DrawSprite(quad);
        }
    }
}
#endif
