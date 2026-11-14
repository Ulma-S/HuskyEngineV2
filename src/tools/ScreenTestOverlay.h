// 【開発ビルドだけ】画面の座標で描けているかを目で確かめるための、試しの重ね描き(UI-2a)。
// コマンド view.screenTest で出す・隠す。出している間は、画面の四隅と真ん中に 48 ピクセルの四角を描く
// (Application::SetScreenDrawCallback から呼ぶ)。ゲームのカメラやエディタのカメラを動かしても、四角は動かない。
// 真ん中の四角(128 ピクセル)には 64 × 64 の試しのテクスチャを貼り、30 フレームごとに 8 × 8 のます目を1つずつ
// 色を変えて書き換える(UI-2b の RenderDevice::UpdateTexture。字のアトラスに字を書き足すのと同じ使い方)。
// 真ん中の上には、日本語のフォントで試しの文字を出す(UI-3b のフォントのアセットと字のアトラス、UI-4 の折り返しと中寄せ)。
// ゲーム用 UI(UI-5〜)ができたら要らなくなる。
// 動作スレッド: ゲームスレッド
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/Handle.h"
#include "core/ObjectRegistry.h"
#include "engine/Application.h"
#include "engine/TextLayout.h"
#include "platform/Image.h"
#include "render/RenderDevice.h"
#include "tools/EditorCommands.h"

namespace husky::tools
{
    class ScreenTestOverlay final
    {
    public:
        ScreenTestOverlay() = default;
        ScreenTestOverlay(const ScreenTestOverlay&) = delete; // 根に入れたフォントを2回外さないように、写させない
        ScreenTestOverlay& operator=(const ScreenTestOverlay&) = delete;
        // 根に入れたフォントを外す。
        ~ScreenTestOverlay();

        // view.screenTest(引数なし、呼ぶたびに出す・隠すを切り替える。見た目だけなので Undo の履歴に積まない)を登録する。
        void RegisterCommands(EditorCommandRegistry& commands);

        // 出していれば、画面の四隅と真ん中に四角、真ん中の上に試しの文字を積む。
        // 行列は画面ちょうどのカメラ(左下が原点、1 単位 = 1 ピクセル)の前提。
        // 試しのテクスチャを、最初に描くときに作り、30 フレームごとに一部を書き換えるので const ではない。
        void Draw(engine::Application& app, render::RenderDevice& renderDevice);

    private:
        // 試しの文字を、左上が (left, top)(画面のピクセル、上が +y)の箱に、折り返しと中寄せで並べて積む(UI-4)。
        void DrawTestText(engine::Application& app, render::RenderDevice& renderDevice, float left, float top);

        bool m_visible = false;
        render::TextureHandle m_texture; // 試しのテクスチャ(64 × 64)。RenderDevice が壊れると一緒に消える
        platform::Image m_cell;          // 書き換える 8 × 8 のます目(入れ物を使い回す)
        int m_frame = 0;                 // 出している間のフレームの数
        int m_nextCell = 0;              // 次に書き換えるます目の番号(0〜63。左上から右へ、行ごとに下へ)

        // 試しの文字のフォント(FontAsset)。ローダが返すハンドルは弱い参照なので、根に入れて回収させない。
        core::ObjectHandle m_font;
        core::ObjectRegistry* m_rootRegistry = nullptr; // 根に入れたレジストリ(外すため。借りているだけ)
        engine::TextLayoutResult m_layout;              // 並べた結果(入れ物を毎フレーム使い回す)
    };
}

#endif
