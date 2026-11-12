// エディタのコマンド(EditorCommandRegistry、シーンの保存のコマンド)と、コマンドを並べるデバッグの窓のテスト(開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

#include "core/MemoryTracker.h"
#include "engine/Actor.h"
#include "engine/JsonFormat.h"
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
    echo.args = {{"text", husky::tools::EditorArgType::String, "text to echo", false}}; // 受け取る引数は、形を書いておく
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

TEST_CASE("EditorCommands: アセットを最新にすると、.meta を作り、スプライトシートを整え、表に入る(ゲームを止めずに足したファイルも)")
{
    husky::tests::TestFolder folder("husky_test_command_refresh");
    folder.WriteAsset("textures/old.png", husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a61ull));
    Application app;
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeRefreshAssetsCommand(folder.Root())));
    const EditorCommand* refresh = commands.Find("asset.refresh");
    REQUIRE(refresh != nullptr);
    CHECK_FALSE(refresh->label.empty());
    CHECK_FALSE(refresh->description.empty());

    EditorCommandResult result = commands.Execute("asset.refresh", app);
    CHECK(result.succeeded);
    CHECK(app.GetAssetDatabase().GetAssetCount() == 1);

    // 動かしている間に、.meta の無い画像と、コマを名前で書いたスプライトシートを足す(人が手で置いたのと同じ)。
    folder.Write("textures/new.png", "dummy");
    folder.Write("sprites/new.spritesheet", R"({ "texture": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a61",
        "sprites": [ { "name": "a", "x": 0, "y": 0, "width": 8, "height": 8 } ],
        "animations": [ { "name": "loop", "frames": ["a"], "frameSeconds": 0.1 } ] })");
    CHECK(app.GetAssetDatabase().FindGuid("textures/new.png") == nullptr); // まだ表に無い

    result = commands.Execute("asset.refresh", app);
    CHECK(result.succeeded);
    CHECK(husky::platform::FileExists(folder.PathOf("textures/new.png.meta").c_str()));
    CHECK(husky::platform::FileExists(folder.PathOf("sprites/new.spritesheet.meta").c_str()));
    CHECK(app.GetAssetDatabase().FindGuid("textures/new.png") != nullptr);
    CHECK(app.GetAssetDatabase().FindGuid("sprites/new.spritesheet") != nullptr);
    CHECK(app.GetAssetDatabase().GetAssetCount() == 3);
    CHECK(result.message.find("created 2 .meta") != std::string::npos);
    CHECK(result.message.find("fixed 1 sprite sheet") != std::string::npos);

    // スプライトシートのコマが、名前から ID に直っている。
    std::string sheetText;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("sprites/new.spritesheet").c_str(), sheetText));
    CHECK(sheetText.find("\"a\"]") == std::string::npos); // frames に名前が残っていない

    // 何も変えずにもう一度: 何も直さず、成功(要約に「作った」「直した」が出ない)。
    result = commands.Execute("asset.refresh", app);
    CHECK(result.succeeded);
    CHECK(result.message == "3 assets");
}

TEST_CASE("EditorCommands: アセットを最新にしたとき、人が直す必要のある問題があれば、表は作ったうえで失敗にし、理由を並べる")
{
    husky::tests::TestFolder folder("husky_test_command_refresh_problem");
    folder.WriteAsset("textures/ok.png", husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a62ull));
    folder.Write("textures/gone.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a63" })"); // 画像だけを消した
    Application app;
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeRefreshAssetsCommand(folder.Root())));

    const EditorCommandResult result = commands.Execute("asset.refresh", app);
    CHECK_FALSE(result.succeeded);
    CHECK(app.GetAssetDatabase().GetAssetCount() == 1); // 問題の無いものは表に入る
    bool listed = false;
    for (const std::string& detail : result.details)
    {
        listed = listed || detail.find(".meta without asset: 1") != std::string::npos;
    }
    CHECK(listed);
}

TEST_CASE("EditorCommands: 引数は実行の前に形で確かめる。足りない・種類が違う・知らない名前なら、実行せずに理由つきで失敗")
{
    Application app;
    EditorCommandRegistry commands;
    int calls = 0;
    EditorCommand command = MakeCountingCommand("test.args", calls);
    command.args = {
        {"name", husky::tools::EditorArgType::String, "a name", true},
        {"size", husky::tools::EditorArgType::Float, "a size", false},
        {"count", husky::tools::EditorArgType::Int, "a count", false},
        {"on", husky::tools::EditorArgType::Bool, "a switch", false},
    };
    CHECK(command.HasRequiredArgs());
    REQUIRE(commands.Register(command));

    const auto run = [&](const char* json) {
        ArchiveValue args;
        if (json != nullptr)
        {
            REQUIRE(husky::engine::JsonFormat::Read(json, args));
        }
        return commands.Execute("test.args", app, args);
    };

    EditorCommandResult result = run(nullptr);
    CHECK_FALSE(result.succeeded);
    CHECK(result.message.find("Missing argument 'name' (string)") != std::string::npos);
    CHECK_FALSE(run(R"({"name": 5})").succeeded);                       // 種類が違う
    CHECK(run(R"({"name": 5})").message.find("wrong type") != std::string::npos);
    CHECK_FALSE(run(R"({"name": "a", "sise": 1})").succeeded);          // 打ち間違い
    CHECK(run(R"({"name": "a", "sise": 1})").message.find("Unknown argument 'sise'") != std::string::npos);
    CHECK_FALSE(run(R"({"name": "a", "count": 1.5})").succeeded);      // 整数のところに小数
    CHECK_FALSE(run(R"({"name": "a", "on": 1})").succeeded);           // bool のところに数
    CHECK(calls == 0); // ここまで、どれも実行していない

    CHECK(run(R"({"name": "a"})").succeeded);                          // 省略できるものは省略してよい
    CHECK(run(R"({"name": "a", "size": 100, "count": 2, "on": true})").succeeded); // float には整数で書いてもよい
    CHECK(calls == 2);
}

TEST_CASE("EditorCommands: アクターを置くコマンドは、型の名前から作り、位置を決める。アクターでない型と知らない型は失敗")
{
    Application app;
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    const EditorCommand* spawn = commands.Find("actor.spawn");
    REQUIRE(spawn != nullptr);
    CHECK(spawn->modifiesScene);

    ArchiveValue args;
    REQUIRE(husky::engine::JsonFormat::Read(R"({"type": "Actor", "x": 100, "y": 50.5})", args));
    const EditorCommandResult result = commands.Execute("actor.spawn", app, args);
    CHECK(result.succeeded);
    CHECK(result.message == "Spawned Actor at (100, 50.5)");
    REQUIRE(app.GetScene().actors.size() == 1);
    const husky::engine::Actor* actor = app.GetRegistry().Resolve<husky::engine::Actor>(app.GetScene().actors[0]);
    REQUIRE(actor != nullptr);
    CHECK(actor->x == 100.0f);
    CHECK(actor->y == 50.5f);
    CHECK(actor->GetScene() == &app.GetScene());

    REQUIRE(husky::engine::JsonFormat::Read(R"({"type": "SpriteComponent"})", args));
    CHECK_FALSE(commands.Execute("actor.spawn", app, args).succeeded); // アクターではない
    REQUIRE(husky::engine::JsonFormat::Read(R"({"type": "NoSuchActor"})", args));
    CHECK_FALSE(commands.Execute("actor.spawn", app, args).succeeded);
    CHECK(app.GetScene().actors.size() == 1);
}

TEST_CASE("EditorCommands: 温まった後は、キーが押されていないフレームのショートカットの確認も、ボタンを並べるのも、メモリを確保しない")
{
    husky::tests::HeadlessImGui imgui;
    Application app;
    EditorCommandRegistry commands;
    int calls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.count_with_a_long_name", calls, Key::F5)));
    REQUIRE(commands.Register(MakeCountingCommand("test.count_without_shortcut", calls)));
    commands.Execute("test.count_with_a_long_name", app); // 最後の結果の行も並べる

    // 1フレーム目で、使い回す入れ物や ImGui の中身が温まる。
    imgui.Frame([&] { husky::tools::DrawDebugWindow(app, &commands); });
    commands.HandleShortcuts(app);

    const std::uint64_t before = husky::core::GetMemoryStats().totalAllocations;
    for (int frame = 0; frame < 10; ++frame)
    {
        imgui.Frame([&] { husky::tools::DrawDebugWindow(app, &commands); });
        commands.HandleShortcuts(app);
    }
    CHECK(husky::core::GetMemoryStats().totalAllocations == before);
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
