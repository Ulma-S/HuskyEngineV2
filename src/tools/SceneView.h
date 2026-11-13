// エディタのシーンビュー(RS-2)。編集中だけ、ゲームのカメラの代わりにエディタのカメラで映し、マウスで見回す・選ぶ。
//   - 右ドラッグか中ドラッグ: 画面を動かす(パン)
//   - ホイール: マウスの下の点を中心に、寄る・引く(ズーム)
//   - 左クリック: マウスの下のアクター(いちばん手前のスプライト)を選ぶ。何も無い所なら選択を外す
//   - 移動のギズモ(RS-4。tools/MoveGizmo.h): 選んだアクターに出る矢印をドラッグすると、その軸だけ動く。真ん中の四角は x と y を
//     一気に動かす。離したときに property.set を1回だけ通すので、Ctrl+Z 1回で元の位置へ戻る(RS-3)。
//     Unity と同じく、アクターの本体をドラッグしても動かさない(左クリックは選ぶだけ。ユーザーが決定)
//   - グリッド(方眼)をシーンより奥に描く(tools/SceneGrid.h)
// マウスが ImGui の窓の上にあるときは、窓の操作を優先して何もしない(io.WantCaptureMouse)。
// 再生中(Play / Pause)は、ゲームのカメラに戻す(エディタのカメラの位置は覚えておき、Stop で戻ると続きから)。
//
// マウスは ImGui が受け取っているもの(ImGui::GetIO())を使う。ゲームのマウス入力(platform / engine::Input)は、
// ゲームで要るようになったら作る(シーンビューは開発ビルドだけの道具なので、ImGui の入力で足りる)。
//
// 毎フレームのメモリの確保はしない。
// 動作スレッド: ゲームスレッドのみ(開発用の UI の中で呼ぶ)
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/Object.h"  // core::ObjectHandle
#include "core/Vector3.h"
#include "engine/Camera.h"
#include "render/RenderDevice.h"
#include "tools/MoveGizmo.h"

#include <vector>

namespace husky::engine
{
    class Application;
    class Actor;
}

namespace husky::tools
{
    class EditorPlayMode;
    class SceneInspector;
    class EditorCommandRegistry;

    class SceneView
    {
    public:
        // 毎フレーム、開発用の UI の関数(Application::SetDevUiCallback)の中で呼ぶ。
        // 選んだアクターは inspector に渡す(インスペクタに出る)。
        // commands があれば、ドラッグで動かした位置を property.set で書く(Undo できる)。無ければ動かすだけ。
        void Update(engine::Application& app, const EditorPlayMode& playMode, SceneInspector& inspector,
                    EditorCommandRegistry* commands = nullptr);

        // グリッド(方眼。tools/SceneGrid.h)を描く。シーンより奥に描くので、Application::SetBeforeSceneDrawCallback から呼ぶ。
        // 線は Update で、そのときのエディタのカメラに合わせて作ってある(編集中だけ。再生中は何も描かない)。
        void DrawGrid(render::RenderDevice& renderDevice) const;

        // 移動のギズモ(tools/MoveGizmo.h)を描く。シーンより手前に描くので、Application::SetAfterSceneDrawCallback から呼ぶ。
        void DrawGizmo(render::RenderDevice& renderDevice) const;

        // 今のエディタのカメラ(テストや、ほかの道具が読むため)。
        const engine::CameraView& GetView() const { return m_view; }

    private:
        engine::CameraView m_view;  // エディタのカメラ
        bool m_initialized = false; // 最初の1回だけ、ゲームのカメラと同じ見え方から始める
        std::vector<render::Sprite> m_gridSprites; // グリッドの線(使い回す入れ物。毎フレーム作り直すが、確保は最初だけ)
        std::vector<render::Sprite> m_gizmoSprites; // 移動のギズモの線(使い回す入れ物)
        GizmoHandle m_hovered = GizmoHandle::None;    // マウスを乗せている(つかんでいる)ギズモの部品(光らせる)
        GizmoHandle m_dragHandle = GizmoHandle::None; // つかんでいるギズモの部品
        core::ObjectHandle m_dragging;   // ギズモで動かしているアクター(弱い参照。動かしていなければ null)
        core::Vector3 m_dragFrom;        // つかむ前の位置(離したときに Undo のための「前の値」にする)
        float m_dragStartX = 0.0f;       // つかんだときのマウスのワールドの位置(ここからの動いた量を軸に足す)
        float m_dragStartY = 0.0f;
    };

    // ドラッグで動かしたアクターの位置を、property.set で書く(RS-3)。actor は今「動かした後の位置」にいる。
    // いったん from(つかむ前の位置)に戻してから property.set を呼ぶので、Undo で from に戻る。
    // コマンドが使えなければ(再生中など)動かした所のまま false を返す。ImGui を使わないので、テストできる。
    // 動作スレッド: ゲームスレッドのみ
    bool CommitActorMove(engine::Application& app, EditorCommandRegistry& commands, engine::Actor& actor,
                         const core::Vector3& from);

    // ホイールのズーム(RS-2)。マウスの下の点(画面の (screenX, screenY))が、ズームの前後で同じワールドの位置に映るように、
    // 真ん中をずらしながら zoom を steps 段(1 段 = 1.1 倍。負なら縮小)変える。zoom は 0.05〜20 に収める。
    // ImGui を使わないので、テストできる。
    // 動作スレッド: どこでもよい
    engine::CameraView ZoomAt(const engine::CameraView& view, float steps, float screenX, float screenY, float screenWidth,
                              float screenHeight);
}
#endif
