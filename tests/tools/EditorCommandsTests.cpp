// エディタのコマンド(EditorCommandRegistry、シーンの保存のコマンド)と、コマンドを並べるデバッグの窓のテスト(開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

#include "platform/FileSystem.h"
#include "tools/DebugWindow.h"
#include "HeadlessImGui.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::platform::Key;
using husky::platform::KeyEvent;
using husky::tools::EditorCommand;
using husky::tools::EditorCommandContext;
using husky::tools::EditorCommandRegistry;
using husky::tools::EditorCommandResult;

namespace
{
    // 呼ばれた回数を数えるだけのコマンド。
    EditorCommand MakeCountingCommand(const char* name, int& calls, Key shortcut = Key::Unknown)
    {
        EditorCommand command;
        command.name = name;
        command.label = "Count";
        command.description = "Count calls";
        command.shortcut = shortcut;
        command.execute = [&calls](EditorCommandContext&) {
            ++calls;
            return EditorCommandResult::Success("counted");
        };
        return command;
    }
}

TEST_CASE("EditorCommands: 名前が空、名前が重なる、実行する関数が無いコマンドは登録できない")
{
    EditorCommandRegistry commands;
    int calls = 0;
    CHECK(commands.Register(MakeCountingCommand("test.count", calls)));
    CHECK_FALSE(commands.Register(MakeCountingCommand("test.count", calls))); // 重なる
    CHECK_FALSE(commands.Register(MakeCountingCommand("", calls)));

    EditorCommand empty;
    empty.name = "test.empty";
    CHECK_FALSE(commands.Register(empty)); // execute が空

    CHECK(commands.GetCommands().size() == 1);
    REQUIRE(commands.Find("test.count") != nullptr);
    CHECK(commands.Find("test.count")->label == "Count");
    CHECK(commands.Find("test.none") == nullptr);
}

TEST_CASE("EditorCommands: 名前で実行し、引数を渡す。結果は「最後の結果」として残る。無い名前は落ちずに失敗を返す")
{
    Application app;
    EditorCommandRegistry commands;
    CHECK(commands.GetLastCommandName().empty());

    EditorCommand echo;
    echo.name = "test.echo";
    echo.label = "Echo";
    echo.execute = [](EditorCommandContext& context) {
        const ArchiveValue* text = context.args.Find("text");
        if (text == nullptr)
        {
            return EditorCommandResult::Failure("text is missing");
        }
        return EditorCommandResult::Success(text->AsString());
    };
    REQUIRE(commands.Register(echo));

    ArchiveValue args = ArchiveValue::MakeObject();
    args.Set("text", ArchiveValue("hello"));
    const EditorCommandResult ok = commands.Execute("test.echo", app, args);
    CHECK(ok.succeeded);
    CHECK(ok.message == "hello");
    CHECK(commands.GetLastCommandName() == "test.echo");
    CHECK(commands.GetLastResult().message == "hello");

    CHECK_FALSE(commands.Execute("test.echo", app).succeeded); // 引数なし: コマンドが自分で失敗を返す

    const EditorCommandResult unknown = commands.Execute("test.none", app);
    CHECK_FALSE(unknown.succeeded);
    CHECK(commands.GetLastCommandName() == "test.none");
    CHECK_FALSE(commands.GetLastResult().succeeded);
}

TEST_CASE("EditorCommands: ショートカットのキーが押されたフレームだけ、そのコマンドを実行する")
{
    Application app;
    EditorCommandRegistry commands;
    int saveCalls = 0;
    int otherCalls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.f5", saveCalls, Key::F5)));
    REQUIRE(commands.Register(MakeCountingCommand("test.none", otherCalls))); // ショートカット無し

    app.StepFrame(0, {KeyEvent{Key::F5, true}}); // F5 を押す
    CHECK(commands.HandleShortcuts(app) == 1);
    app.StepFrame(0, {}); // 押したまま(押した瞬間ではない)
    CHECK(commands.HandleShortcuts(app) == 0);
    app.StepFrame(0, {KeyEvent{Key::F5, false}});
    CHECK(commands.HandleShortcuts(app) == 0);

    CHECK(saveCalls == 1);
    CHECK(otherCalls == 0);
}

TEST_CASE("EditorCommands: シーンの保存は、指定したすべてのファイルに書き、書いた場所を細かい内容として返す")
{
    husky::tests::TestFolder folder("husky_test_command_save");
    Application app;
    app.GetScene().Spawn<husky::engine::Actor>();

    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSaveSceneCommand({folder.PathOf("a.json"), folder.PathOf("b.json")})));
    const EditorCommand* save = commands.Find("scene.save");
    REQUIRE(save != nullptr);
    CHECK(save->shortcut == Key::F5);
    CHECK_FALSE(save->label.empty());
    CHECK_FALSE(save->description.empty());

    const EditorCommandResult result = commands.Execute("scene.save", app);
    CHECK(result.succeeded);
    CHECK(result.details.size() == 2);
    CHECK(husky::platform::FileExists(folder.PathOf("a.json").c_str()));
    CHECK(husky::platform::FileExists(folder.PathOf("b.json").c_str()));

    // 書けた側を読み戻すと、同じシーン(アクター1つ)になる。
    Application loaded;
    REQUIRE(loaded.LoadSceneFile(folder.PathOf("b.json")));
    CHECK(loaded.GetScene().actors.size() == 1);
}

TEST_CASE("EditorCommands: シーンの保存で書けないファイルがあれば失敗にし、書けなかった場所を返す")
{
    husky::tests::TestFolder folder("husky_test_command_save_fail");
    Application app;

    EditorCommandRegistry commands;
    // 無いフォルダの中には書けない(platform::WriteTextFile はフォルダを作らない)。
    REQUIRE(commands.Register(
        husky::tools::MakeSaveSceneCommand({folder.PathOf("ok.json"), folder.PathOf("no_such_folder/ng.json")})));
    const EditorCommandResult result = commands.Execute("scene.save", app);
    CHECK_FALSE(result.succeeded);
    REQUIRE(result.details.size() == 1);
    CHECK(result.details[0].find("ng.json") != std::string::npos);
    CHECK(husky::platform::FileExists(folder.PathOf("ok.json").c_str())); // 書けるものは書く
}

TEST_CASE("DebugWindow: コマンドのボタンと最後の結果を並べても、ImGui の使い方の誤りが無い。並べるだけでは実行しない")
{
    husky::tests::HeadlessImGui imgui;
    Application app;
    EditorCommandRegistry commands;
    int calls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.count", calls, Key::F5)));

    imgui.Frame([&] { husky::tools::DrawDebugWindow(app, &commands); }); // 結果がまだ無いとき
    commands.Execute("test.count", app);
    imgui.Frame([&] { husky::tools::DrawDebugWindow(app, &commands); }); // 結果があるとき
    imgui.Frame([&] { husky::tools::DrawDebugWindow(app, nullptr); });   // コマンド無し
    CHECK(calls == 1);
    CHECK(std::string(husky::tools::GetKeyDisplayName(Key::F5)) == "F5");
}

#endif
