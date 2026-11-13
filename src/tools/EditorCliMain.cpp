// エディタの CLI(HuskyEditorCli.exe)のエントリポイント。中身は tools::RunEditorCli(EditorCli.h)を呼ぶだけ。
// 開発ビルドだけで使える。製品(Release)のビルドでは、使えないことを伝えて終わる。
#include "core/Log.h"
#include "platform/MemoryHooks.h"
#include "platform/Platform.h"

#if HUSKY_DEVELOPMENT
#include "game/Game.h" // このゲームの型(エディタもゲームのコードを読み込む)
#include "tools/EditorCli.h"
#endif

#include <cstdio> // std::fputs
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    // 一番最初に: SDL のメモリの確保を MemoryTracker に通す(ゲームとテストの main と同じ。platform/MemoryHooks.h)。
    husky::platform::InstallMemoryHooks();
    // ログの日本語を、コンソールで文字化けさせない。
    husky::platform::EnableUtf8ConsoleOutput();
    // ログを画面に出す(main を抜けるときに外れる。core/Log.h)。
    husky::core::ConsoleLogSink consoleLog;
    husky::core::ScopedLogSink attachConsoleLog(consoleLog);

#if HUSKY_DEVELOPMENT
    // argv[0] は exe の名前なので飛ばす。char* の並びを、扱いやすい std::string の並びに直す。
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    std::string out;
    // アセットのフォルダの既定は、ソースの assets(開発ビルドのゲームと同じ)。
    // このゲーム用のエディタなので、ゲームの型を登録する(シーンの "type": "BouncerActor" を作れるように)。
    const int exitCode = husky::tools::RunEditorCli(
        args, std::string(HUSKY_SOURCE_DIR) + "assets", out,
        [](husky::engine::Application& app) { husky::game::RegisterGameTypes(app.GetTypeRegistry()); });
    std::fputs(out.c_str(), stdout);
    return exitCode;
#else
    (void)argc;
    (void)argv;
    std::fputs("HuskyEditorCli is available only in development builds (Debug).\n", stderr);
    return 2;
#endif
}
