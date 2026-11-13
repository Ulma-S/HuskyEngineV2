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
    // ログを画面に出す(main を抜けるときに外れる。core/Log.h)。標準エラーに出し、標準出力はコマンドの結果だけにする
    // (ほかのプログラムが結果を読むとき、ログが混ざらないように)。
    husky::core::ConsoleLogSink consoleLog(husky::core::LogLevel::Info,
                                           husky::core::ConsoleLogSink::Stream::StandardError);
    husky::core::ScopedLogSink attachConsoleLog(consoleLog);
    husky::platform::ForwardSdlLogToEngine(); // SDL 自身のログもエンジンのログへ

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
    // 人向けの結果は標準出力へ。ただし --json-out - (結果の JSON を標準出力へ)のときは、JSON だけが標準出力に
    // 来るように、人向けの結果は標準エラーへ回す。
    bool jsonToStandardOutput = false;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
    {
        jsonToStandardOutput = jsonToStandardOutput || (args[i] == "--json-out" && args[i + 1] == "-");
    }
    std::fputs(out.c_str(), jsonToStandardOutput ? stderr : stdout);
    return exitCode;
#else
    (void)argc;
    (void)argv;
    std::fputs("HuskyEditorCli is available only in development builds (Debug).\n", stderr);
    return 2;
#endif
}
