// エディタのシーンビュー(RS-2)。編集中だけ、ゲームのカメラの代わりにエディタのカメラで映し、マウスで見回す・選ぶ。
//   - 右ドラッグか中ドラッグ: 画面を動かす(パン)
//   - ホイール: マウスの下の点を中心に、寄る・引く(ズーム)
//   - 左クリック: マウスの下のアクター(いちばん手前のスプライト)を選ぶ。何も無い所なら選択を外す
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

#include "engine/Camera.h"

namespace husky::engine
{
    class Application;
}

namespace husky::tools
{
    class EditorPlayMode;
    class SceneInspector;

    class SceneView
    {
    public:
        // 毎フレーム、開発用の UI の関数(Application::SetDevUiCallback)の中で呼ぶ。
        // 選んだアクターは inspector に渡す(インスペクタに出る)。
        void Update(engine::Application& app, const EditorPlayMode& playMode, SceneInspector& inspector);

        // 今のエディタのカメラ(テストや、ほかの道具が読むため)。
        const engine::CameraView& GetView() const { return m_view; }

    private:
        engine::CameraView m_view;  // エディタのカメラ
        bool m_initialized = false; // 最初の1回だけ、ゲームのカメラと同じ見え方から始める
    };

    // ホイールのズーム(RS-2)。マウスの下の点(画面の (screenX, screenY))が、ズームの前後で同じワールドの位置に映るように、
    // 真ん中をずらしながら zoom を steps 段(1 段 = 1.1 倍。負なら縮小)変える。zoom は 0.05〜20 に収める。
    // ImGui を使わないので、テストできる。
    // 動作スレッド: どこでもよい
    engine::CameraView ZoomAt(const engine::CameraView& view, float steps, float screenX, float screenY, float screenWidth,
                              float screenHeight);
}
#endif
