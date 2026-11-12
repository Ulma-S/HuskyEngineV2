// 開発用のデバッグの窓(ImGui)。フレームの時間、描画、オブジェクト、メモリの数字を出し、シーンの保存のボタンを置く。
// tools モジュールの最初の部品。製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// 使い方(main.cpp):
//   app.SetDevUiCallback([&](engine::Application& a) { tools::DrawDebugWindow(a, actions); });
//
// 窓の文字は、今は英語で書く(ImGui の既定のフォントには日本語が無いため。日本語のフォントを入れるかは後で相談する)。
//
// 動作スレッド: ゲームスレッドのみ(Application::SetDevUiCallback から呼ばれる)。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"

#include <functional>

namespace husky::tools
{
    // 窓のボタンが押されたときにすること。ゲームの側が決めて渡す(保存先の場所などは、ゲームしか知らないため)。
    struct DebugWindowActions
    {
        std::function<void()> saveScene; // 「Save scene」ボタン。空なら、ボタンを出さない
    };

    // デバッグの窓を作る。毎フレーム、開発用の UI のフレームの中で呼ぶ。
    void DrawDebugWindow(engine::Application& app, const DebugWindowActions& actions);
}

#endif
