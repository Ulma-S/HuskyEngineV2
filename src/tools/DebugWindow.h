// 開発用のデバッグの窓(ImGui)。フレームの時間、描画、オブジェクト、メモリの数字と、エディタのコマンドのボタンを出す。
// tools モジュールの最初の部品。製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// 使い方(main.cpp):
//   app.SetDevUiCallback([&](engine::Application& a) { tools::DrawDebugWindow(a, &commands); });
//
// 窓の文字は、今は英語で書く(ImGui の既定のフォントには日本語が無いため。日本語のフォントを入れるかは後で相談する)。
//
// 動作スレッド: ゲームスレッドのみ(Application::SetDevUiCallback から呼ばれる)。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "tools/EditorCommands.h"

namespace husky::tools
{
    // デバッグの窓を作る。毎フレーム、開発用の UI のフレームの中で呼ぶ。
    // commands: 「Editor commands」の欄に、登録されたコマンドをボタンで並べ、最後に実行した結果を出す。
    //           ボタンは名前でコマンドを呼ぶだけ(何をするかは、コマンドの側にある)。nullptr なら、欄を出さない。
    void DrawDebugWindow(engine::Application& app, EditorCommandRegistry* commands);

    // キーの、人に見せる名前("F5" など)。ショートカットの表示に使う。
    const char* GetKeyDisplayName(engine::Key key);
}

#endif
