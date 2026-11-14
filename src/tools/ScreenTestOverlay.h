// 【開発ビルドだけ】画面の座標で描けているかを目で確かめるための、試しの重ね描き(UI-2a)。
// コマンド view.screenTest で出す・隠す。出している間は、画面の四隅と真ん中に 48 ピクセルの四角を描く
// (Application::SetScreenDrawCallback から呼ぶ)。ゲームのカメラやエディタのカメラを動かしても、四角は動かない。
// ゲーム用 UI(UI-5〜)ができたら要らなくなる。
// 動作スレッド: ゲームスレッド
#pragma once

#if HUSKY_DEVELOPMENT

#include "render/RenderDevice.h"
#include "tools/EditorCommands.h"

namespace husky::tools
{
    class ScreenTestOverlay final
    {
    public:
        // view.screenTest(引数なし、呼ぶたびに出す・隠すを切り替える。見た目だけなので Undo の履歴に積まない)を登録する。
        void RegisterCommands(EditorCommandRegistry& commands);

        // 出していれば、画面の四隅と真ん中に四角を積む。行列は画面ちょうどのカメラ(左下が原点、1 単位 = 1 ピクセル)の前提。
        void Draw(render::RenderDevice& renderDevice) const;

    private:
        bool m_visible = false;
    };
}

#endif
