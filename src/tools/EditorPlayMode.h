// エディタの編集モードと、再生(Play)・一時停止(Pause)・停止(Stop)。Unity の Play ボタンにあたる。
// 製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。製品のゲームは、最初からずっと再生している。
//
// 状態:
//   Edit(編集)  … シーンの更新(Tick)を止める。置いたものは動かない。保存はこの状態でだけできる
//   Playing(再生)… シーンを動かす。始めるときに、今のシーンをメモリに控える
//   Paused(一時停止)… 再生の途中で止める。Step で 1 フレームずつ進められる
// Stop で、控えからシーンを読み直して Edit に戻る。再生中に動いたもの・壊れたもの・変えた値は、すべて元に戻る。
// どの状態でも、描画、アセットの読み込み、GC、開発用の UI は動く(止めるのはシーンの Tick だけ)。
//
// 控えは、シーンを保存するのと同じ形(SceneSerializer の ArchiveValue)で、ファイルには書かずメモリに持つ。
// ただのデータ(GC 管理のオブジェクトではない)なので、控えている間に GC が回っても困らない。
//
// 操作はエディタのコマンド(editor.play / editor.pause / editor.step / editor.stop)として登録する(RegisterCommands)。
// ボタンも、将来の CLI や AI も、このコマンドを名前で呼ぶ。
//
// 使い方(main.cpp):
//   tools::EditorPlayMode playMode;
//   playMode.EnterEditMode(app);                     // 開発ビルドは編集の状態で始める
//   playMode.RegisterCommands(commands);
//   commands.Execute("editor.play", app);
//
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/ArchiveValue.h"
#include "tools/EditorCommands.h"

namespace husky::tools
{
    class EditorPlayMode
    {
    public:
        enum class State
        {
            Edit,
            Playing,
            Paused,
        };

        State GetState() const { return m_state; }
        // 画面に出す、状態の名前("EDIT" など)。
        static const char* GetStateName(State state);

        // 編集の状態にする(シーンの Tick を止める)。控えは捨てる。起動のときに1回呼ぶ。
        void EnterEditMode(engine::Application& app);

        // それぞれの操作。今の状態で使えなければ、何もせずに失敗の結果を返す(理由つき)。
        EditorCommandResult Play(engine::Application& app);  // Edit → Playing(控えを取る)、Paused → Playing(続きから)
        EditorCommandResult Pause(engine::Application& app); // Playing → Paused
        EditorCommandResult Step(engine::Application& app);  // Paused のまま、次のフレームで1回だけ進める
        EditorCommandResult Stop(engine::Application& app);  // Playing / Paused → Edit(控えからシーンを戻す)

        // 今の状態で、それぞれの操作が使えない理由(使えるなら nullptr)。コマンドの whyUnavailable に使う。
        const char* WhyCannotPlay() const;
        const char* WhyCannotPause() const;
        const char* WhyCannotStep() const;
        const char* WhyCannotStop() const;
        // 編集の状態でしかできない操作(シーンの保存など)に付ける理由。
        const char* WhyNotEditing() const;

        // editor.play / editor.pause / editor.step / editor.stop を commands に登録する。
        // コマンドは this を覚えるので、この EditorPlayMode は commands より長く生きていること。
        void RegisterCommands(EditorCommandRegistry& commands);

    private:
        State m_state = State::Playing; // EnterEditMode を呼ぶまでは、ふつうのゲームと同じく動いている
        engine::ArchiveValue m_snapshot; // Play のときに控えたシーン(Edit の間は空)
    };
}

#endif
