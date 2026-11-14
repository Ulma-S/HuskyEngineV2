// 【開発ビルドだけ】画面の座標で描けているかを目で確かめるための、試しの重ね描き(UI-2a)。
// コマンド view.screenTest で出す・隠す。出している間は、画面の四隅と真ん中に 48 ピクセルの四角を描く
// (Application::SetScreenDrawCallback から呼ぶ)。ゲームのカメラやエディタのカメラを動かしても、四角は動かない。
// 真ん中の四角(128 ピクセル)には 64 × 64 の試しのテクスチャを貼り、30 フレームごとに 8 × 8 のます目を1つずつ
// 色を変えて書き換える(UI-2b の RenderDevice::UpdateTexture。字のアトラスに字を書き足すのと同じ使い方)。
// ゲーム用 UI(UI-5〜)ができたら要らなくなる。
// 動作スレッド: ゲームスレッド
#pragma once

#if HUSKY_DEVELOPMENT

#include "platform/Image.h"
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
        // 試しのテクスチャを、最初に描くときに作り、30 フレームごとに一部を書き換えるので const ではない。
        void Draw(render::RenderDevice& renderDevice);

    private:
        bool m_visible = false;
        render::TextureHandle m_texture; // 試しのテクスチャ(64 × 64)。RenderDevice が壊れると一緒に消える
        platform::Image m_cell;          // 書き換える 8 × 8 のます目(入れ物を使い回す)
        int m_frame = 0;                 // 出している間のフレームの数
        int m_nextCell = 0;              // 次に書き換えるます目の番号(0〜63。左上から右へ、行ごとに下へ)
    };
}

#endif
