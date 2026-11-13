// エディタのコマンド(EditorCommandRegistry、シーンの保存のコマンド)と、コマンドを並べるデバッグの窓のテスト(開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

#include "core/MemoryTracker.h"
#include "engine/Actor.h"
#include "engine/CameraComponent.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "game/CameraFollowComponent.h"
#include "platform/FileSystem.h"
#include "tools/DebugWindow.h"
#include "HeadlessImGui.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <algorithm> // std::find
#include <string>
#include <vector>

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

TEST_CASE("EditorCommands: Ctrl 付きのショートカットは Ctrl を押しているときだけ、Ctrl 無しのものは押していないときだけ動く")
{
    Application app;
    EditorCommandRegistry commands;
    int undoCalls = 0;
    int zCalls = 0;
    EditorCommand undo = MakeCountingCommand("test.ctrl_z", undoCalls, Key::Z);
    undo.shortcutCtrl = true;
    REQUIRE(commands.Register(std::move(undo)));
    REQUIRE(commands.Register(MakeCountingCommand("test.z", zCalls, Key::Z)));

    app.StepFrame(0, {KeyEvent{Key::Z, true}}); // Z だけ
    CHECK(commands.HandleShortcuts(app) == 1);
    CHECK(zCalls == 1);
    CHECK(undoCalls == 0);
    app.StepFrame(0, {KeyEvent{Key::Z, false}});

    app.StepFrame(0, {KeyEvent{Key::RightCtrl, true}, KeyEvent{Key::Z, true}}); // Ctrl+Z(右の Ctrl でもよい)
    CHECK(commands.HandleShortcuts(app) == 1);
    CHECK(undoCalls == 1);
    CHECK(zCalls == 1);
    app.StepFrame(0, {}); // 押したまま
    CHECK(commands.HandleShortcuts(app) == 0);
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
    CHECK(actor->transform.position.x == 100.0f);
    CHECK(actor->transform.position.y == 50.5f);
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
    husky::tools::DebugWindow window; // 入力欄の値やボタンの文字の入れ物を持つので、フレームをまたいで使う
    Application app;
    EditorCommandRegistry commands;
    int calls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.count_with_a_long_name", calls, Key::F5)));
    REQUIRE(commands.Register(MakeCountingCommand("test.count_without_shortcut", calls)));
    commands.Execute("test.count_with_a_long_name", app); // 最後の結果の行も並べる

    // 1フレーム目で、使い回す入れ物や ImGui の中身が温まる。
    imgui.Frame([&] { window.Draw(app, &commands); });
    commands.HandleShortcuts(app);

    const std::uint64_t before = husky::core::GetMemoryStats().totalAllocations;
    for (int frame = 0; frame < 10; ++frame)
    {
        imgui.Frame([&] { window.Draw(app, &commands); });
        commands.HandleShortcuts(app);
    }
    CHECK(husky::core::GetMemoryStats().totalAllocations == before);
}

TEST_CASE("DebugWindow: 入力欄は引数の形の既定値から始まり、Run で渡す引数は、必須のものと、既定値があるか触ったものだけ")
{
    int calls = 0;
    EditorCommand command = MakeCountingCommand("test.inputs", calls);
    command.args = {
        {"name", husky::tools::EditorArgType::String, "a name", true},
        {"size", husky::tools::EditorArgType::Float, "a size", false, ArchiveValue(2.5)},
        {"count", husky::tools::EditorArgType::Int, "a count", false},
    };
    husky::tools::DebugWindow window;
    std::vector<husky::tools::CommandArgInput>& inputs = window.GetArgInputs(command);
    REQUIRE(inputs.size() == 3);
    CHECK(inputs[1].number == 2.5);
    CHECK(inputs[1].send);       // 既定値があるものは渡す
    CHECK_FALSE(inputs[2].send); // 既定値が無い省略できるものは、触るまで渡さない

    inputs[0].text = "abc";
    ArchiveValue args = husky::tools::DebugWindow::BuildArgs(command, inputs);
    CHECK(args.Find("name")->AsString() == "abc");
    CHECK(args.Find("size")->AsFloat() == 2.5);
    CHECK(args.Find("count") == nullptr);
    CHECK(husky::tools::ValidateCommandArgs(command, args).empty()); // 作った引数は、形の確かめを通る

    inputs[2].integer = 7;
    inputs[2].send = true; // 人が触った
    args = husky::tools::DebugWindow::BuildArgs(command, inputs);
    CHECK(args.Find("count")->AsInt() == 7);
    CHECK(&window.GetArgInputs(command) == &inputs); // 2回目からは同じ値(閉じても残る)
}

TEST_CASE("EditorCommands: prefab.create でアクターからプレハブを作り、actor.spawn の prefab で見た目つきで置ける")
{
    husky::tests::TestFolder folder("husky_test_prefab_commands");
    folder.Write("readme.txt", "assets"); // 空のフォルダは Scan できないことがあるので、1つ置いておく
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    CHECK(commands.Find("prefab.create")->readsScene);

    husky::engine::Actor* coin = app.GetScene().Spawn<husky::engine::Actor>();
    coin->name = "Coin";
    coin->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;

    // --- prefab.create: ファイルと .meta ができ、アセットの表にも入る ---
    ArchiveValue args;
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})", args));
    const EditorCommandResult created = commands.Execute("prefab.create", app, args);
    REQUIRE(created.succeeded);
    CHECK(husky::platform::FileExists(folder.PathOf("prefabs/coin.prefab").c_str()));
    CHECK(husky::platform::FileExists(folder.PathOf("prefabs/coin.prefab.meta").c_str()));
    const std::string guidText = created.data.Find("guid")->AsString();
    const husky::core::Guid* tableGuid = app.GetAssetDatabase().FindGuid("prefabs/coin.prefab");
    REQUIRE(tableGuid != nullptr);
    CHECK(tableGuid->ToString() == guidText);
    CHECK_FALSE(commands.Execute("prefab.create", app, args).succeeded); // すでにある(上書きしない)
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Coin", "path": "../coin.prefab"})", args));
    CHECK_FALSE(commands.Execute("prefab.create", app, args).succeeded); // assets の外
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Coin", "path": "coin.json"})", args));
    CHECK_FALSE(commands.Execute("prefab.create", app, args).succeeded); // .prefab ではない

    // --- actor.spawn の prefab: パスでも GUID でも指せ、コンポーネントと値が付き、元のプレハブを覚える ---
    REQUIRE(husky::engine::JsonFormat::Read(R"({"prefab": "prefabs/coin.prefab", "x": 5})", args));
    const EditorCommandResult spawned = commands.Execute("actor.spawn", app, args);
    REQUIRE(spawned.succeeded);
    CHECK(spawned.message == "Spawned prefabs/coin.prefab at (5, 0)");
    REQUIRE(app.GetScene().actors.size() == 2);
    husky::engine::Actor* placed = app.GetRegistry().Resolve<husky::engine::Actor>(app.GetScene().actors[1]);
    REQUIRE(placed != nullptr);
    CHECK(placed->name == "Coin");
    CHECK(placed->transform.position.x == 5.0f);
    CHECK(placed->guid.ToString() == spawned.data.Find("guid")->AsString());
    REQUIRE(placed->GetComponent<husky::engine::SpriteComponent>() != nullptr);
    CHECK(placed->GetComponent<husky::engine::SpriteComponent>()->width == 24.0f);
    CHECK(placed->prefab.guid.ToString() == guidText);
    CHECK(coin->prefab.IsNull()); // 元のアクターは、プレハブにつながらない

    // 置いたアクターからプレハブを作っても、ファイルには guid と元のプレハブを書かない(シーンの中の1体を表す値なので)
    ArchiveValue fromPlaced = ArchiveValue::MakeObject();
    fromPlaced.Set("actor", ArchiveValue(placed->guid.ToString()));
    fromPlaced.Set("path", ArchiveValue("prefabs/coin2.prefab"));
    REQUIRE(commands.Execute("prefab.create", app, fromPlaced).succeeded);
    std::string text;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("prefabs/coin2.prefab").c_str(), text));
    CHECK(text.find("\"prefab\"") == std::string::npos);
    CHECK(text.find("\"guid\"") == std::string::npos);

    ArchiveValue byGuid = ArchiveValue::MakeObject();
    byGuid.Set("prefab", ArchiveValue(guidText));
    CHECK(commands.Execute("actor.spawn", app, byGuid).succeeded);
    CHECK(app.GetScene().actors.size() == 3);

    // prefab と type は、どちらか一方だけ
    REQUIRE(husky::engine::JsonFormat::Read(R"({"prefab": "prefabs/coin.prefab", "type": "Actor"})", args));
    CHECK_FALSE(commands.Execute("actor.spawn", app, args).succeeded);
    CHECK_FALSE(commands.Execute("actor.spawn", app, ArchiveValue::MakeObject()).succeeded);
    REQUIRE(husky::engine::JsonFormat::Read(R"({"prefab": "prefabs/none.prefab"})", args));
    CHECK_FALSE(commands.Execute("actor.spawn", app, args).succeeded);
    CHECK(app.GetScene().actors.size() == 3);
}

TEST_CASE("DebugWindow: 引数の入力欄の小窓(選択肢の箱を含む)を開いても、ImGui の使い方の誤りが無い。開くだけでは実行しない")
{
    husky::tests::HeadlessImGui imgui;
    husky::tools::DebugWindow window;
    Application app;
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));

    window.OpenArgsInput("actor.spawn");
    imgui.Frame([&] { window.Draw(app, &commands); }); // 小窓を開いたフレーム
    imgui.Frame([&] { window.Draw(app, &commands); }); // 開いたまま
    CHECK(app.GetScene().actors.empty());
    CHECK(commands.GetLastCommandName().empty());

    // 選択肢: 型の登録簿にある Actor の子孫(アクターでない型は入らない)
    std::vector<std::string> values;
    for (const husky::tools::EditorArgChoice& choice : commands.Find("actor.spawn")->args[1].choices(app, ArchiveValue()))
    {
        values.push_back(choice.value);
    }
    CHECK(std::find(values.begin(), values.end(), "Actor") != values.end());
    CHECK(std::find(values.begin(), values.end(), "SpriteComponent") == values.end());
}

namespace
{
    // property.set と actor.list のテスト用のシーン: 名前の重なる Enemy が2体と、Player が1体。
    struct NamedActors
    {
        husky::engine::Actor* enemyA = nullptr;
        husky::engine::Actor* enemyB = nullptr;
        husky::engine::Actor* player = nullptr;
    };

    NamedActors SpawnNamedActors(Application& app)
    {
        NamedActors actors;
        actors.enemyA = app.GetScene().Spawn<husky::engine::Actor>();
        actors.enemyB = app.GetScene().Spawn<husky::engine::Actor>();
        actors.player = app.GetScene().Spawn<husky::engine::Actor>();
        actors.enemyA->name = "Enemy";
        actors.enemyB->name = "Enemy";
        actors.player->name = "Player";
        actors.enemyB->transform.position.x = 50.0f;
        return actors;
    }

    ArchiveValue Json(const char* text)
    {
        ArchiveValue value;
        REQUIRE(husky::engine::JsonFormat::Read(text, value));
        return value;
    }
}

TEST_CASE("EditorCommands: アクターは名前でも guid でも指せる。名前が重なっていたら、候補を並べて断る")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    std::string error;

    CHECK(husky::tools::FindActorFromArg(app.GetScene(), "Player", error) == actors.player);
    CHECK(husky::tools::FindActorFromArg(app.GetScene(), actors.enemyB->guid.ToString(), error) == actors.enemyB);

    CHECK(husky::tools::FindActorFromArg(app.GetScene(), "Enemy", error) == nullptr);
    CHECK(error.find("matches 2 actors") != std::string::npos);
    CHECK(error.find(actors.enemyA->guid.ToString()) != std::string::npos); // 候補の guid が並ぶ
    CHECK(error.find(actors.enemyB->guid.ToString()) != std::string::npos);

    CHECK(husky::tools::FindActorFromArg(app.GetScene(), "Nobody", error) == nullptr);
    CHECK(error.find("No actor named") != std::string::npos);
    CHECK(husky::tools::FindActorFromArg(app.GetScene(), "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a99", error) == nullptr);
    CHECK(error.find("No actor has the guid") != std::string::npos);
}

TEST_CASE("EditorCommands: actor.list はアクターを名前、型、guid で並べ、data に全件を入れる。名前と型で絞り込める")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeListActorsCommand()));

    EditorCommandResult result = commands.Execute("actor.list", app);
    CHECK(result.succeeded);
    CHECK(result.message == "3 actor(s)");
    REQUIRE(result.data.GetArraySize() == 3);
    CHECK(result.data.At(2).Find("name")->AsString() == "Player");
    CHECK(result.data.At(2).Find("guid")->AsString() == actors.player->guid.ToString());
    CHECK(result.data.At(2).Find("type")->AsString() == "Actor");

    result = commands.Execute("actor.list", app, Json(R"({"name": "Enemy"})"));
    CHECK(result.data.GetArraySize() == 2);
    result = commands.Execute("actor.list", app, Json(R"({"type": "Scene"})")); // アクターでない型には当てはまらない
    CHECK(result.data.GetArraySize() == 0);
    CHECK_FALSE(commands.Execute("actor.list", app, Json(R"({"type": "NoSuchType"})")).succeeded);
}

TEST_CASE("EditorCommands: property.set はアクターのプロパティを書き換え、前の値も返す。種類の違う値、guid、無いプロパティは断る")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));

    EditorCommandResult result = commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": 120})"));
    CHECK(result.succeeded);
    CHECK(actors.player->transform.position.x == 120.0f);
    CHECK(result.message == "Set Player (Actor).transform.position.x = 120");
    REQUIRE_FALSE(result.details.empty());
    CHECK(result.details[0] == "was 0.0");

    // 名前が重なっていると、名前では指せない(2体いる)
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Enemy", "property": "transform.position.x", "value": 1})")).succeeded);
    CHECK(actors.enemyA->transform.position.x == 0.0f);

    // 名前が重なっていても、guid なら指せる
    const std::string args = std::string(R"({"actor": ")") + actors.enemyB->guid.ToString() +
                             R"(", "property": "name", "value": "Boss"})";
    CHECK(commands.Execute("property.set", app, Json(args.c_str())).succeeded);
    CHECK(actors.enemyB->name == "Boss");
    CHECK(actors.enemyA->name == "Enemy"); // もう1体は変わらない

    // 断るもの(どれも値は変えない)
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": "left"})")).succeeded);
    CHECK(actors.player->transform.position.x == 120.0f);
    const husky::core::Guid guidBefore = actors.player->guid;
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "guid", "value": "x"})")).succeeded);
    CHECK(actors.player->guid == guidBefore);
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "speed", "value": 1})")).succeeded);

    // プロパティの選択肢は、選んだアクターの書き換えられるプロパティ(guid と components は入らない)
    std::vector<std::string> names;
    for (const husky::tools::EditorArgChoice& choice :
         commands.Find("property.set")->args[2].choices(app, Json(R"({"actor": "Player"})")))
    {
        names.push_back(choice.value);
    }
    CHECK(std::find(names.begin(), names.end(), "transform.position.x") != names.end());
    CHECK(std::find(names.begin(), names.end(), "guid") == names.end());
    CHECK(std::find(names.begin(), names.end(), "components") == names.end());

    // 構造体(Transform)の中は「点でつないだ道筋」で指す。書いた値だけが変わり、ほかの値はそのまま。
    actors.player->transform.position.z = 3.0f;
    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.y", "value": 7})")).succeeded);
    CHECK(actors.player->transform.position.x == 120.0f);
    CHECK(actors.player->transform.position.y == 7.0f);
    CHECK(actors.player->transform.position.z == 3.0f);
    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.rotation", "value": 45})")).succeeded);
    CHECK(actors.player->transform.rotation == 45.0f);
    // 構造体に構造体ではない値、構造体の中に無い名前、構造体ではないものの中は、断る
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform", "value": 1})")).succeeded);
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.w", "value": 1})")).succeeded);
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "name.x", "value": 1})")).succeeded);
    CHECK(actors.player->transform.position.y == 7.0f);

    // アクターの選択肢は、表示が人向け(名前と型)で、値が guid
    const std::vector<husky::tools::EditorArgChoice> actorChoices =
        commands.Find("property.set")->args[0].choices(app, ArchiveValue());
    REQUIRE(actorChoices.size() == 3);
    CHECK(actorChoices[2].value == actors.player->guid.ToString());
    CHECK(actorChoices[2].label == "Player (Actor)");
}

TEST_CASE("EditorCommands: property.set はコンポーネントのプロパティも書き換えられ、構造体は書いた値だけが変わる")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    auto* sprite = actors.player->AddComponent<husky::engine::SpriteComponent>();
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));

    EditorCommandResult result = commands.Execute(
        "property.set", app, Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "width", "value": 64})"));
    CHECK(result.succeeded);
    CHECK(sprite->width == 64.0f);
    CHECK(result.message == "Set Player (Actor).SpriteComponent.width = 64");

    // 構造体そのものを {"名前": 値} で渡すと、書いた値だけが変わる
    CHECK(commands.Execute("property.set", app,
                           Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "color", "value": {"g": 0.5}})"))
              .succeeded);
    CHECK(sprite->color.r == 1.0f);
    CHECK(sprite->color.g == 0.5f);
    // 構造体の一部の値が合わなければ、ほかの値も書かずに断る(前の値に戻す)
    CHECK_FALSE(commands.Execute("property.set", app,
                                 Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "color", "value": {"r": 0, "b": "blue"}})"))
                    .succeeded);
    CHECK(sprite->color.r == 1.0f);

    // アセットの参照は GUID の文字列で指し、null で外す(Undo で前の GUID に戻せる)
    CHECK(commands.Execute("property.set", app,
                           Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "texture", "value": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a61"})"))
              .succeeded);
    CHECK(sprite->texture.guid.ToString() == "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a61");
    result = commands.Execute("property.set", app,
                              Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "texture", "value": null})"));
    CHECK(result.succeeded);
    CHECK(sprite->texture.IsNull());
    CHECK(result.undo.args.Find("value")->AsString() == "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a61");
    CHECK_FALSE(commands.Execute("property.set", app,
                                 Json(R"({"actor": "Player", "component": "SpriteComponent", "property": "texture", "value": "not a guid"})"))
                    .succeeded);

    // 付いていないコンポーネント、知らない型は、付いているものを見せて断る
    result = commands.Execute("property.set", app,
                              Json(R"({"actor": "Player", "component": "CameraComponent", "property": "zoom", "value": 2})"));
    CHECK_FALSE(result.succeeded);
    CHECK(result.message.find("SpriteComponent") != std::string::npos);
    CHECK_FALSE(commands.Execute("property.set", app,
                                 Json(R"({"actor": "Player", "component": "Nope", "property": "width", "value": 1})"))
                    .succeeded);

    // 選択肢: コンポーネントは付いているもの、プロパティは選んだコンポーネントのもの
    const EditorCommand& command = *commands.Find("property.set");
    const std::vector<husky::tools::EditorArgChoice> components = command.args[1].choices(app, Json(R"({"actor": "Player"})"));
    REQUIRE(components.size() == 1);
    CHECK(components[0].value == "SpriteComponent");
    std::vector<std::string> names;
    for (const husky::tools::EditorArgChoice& choice :
         command.args[2].choices(app, Json(R"({"actor": "Player", "component": "SpriteComponent"})")))
    {
        names.push_back(choice.value);
    }
    CHECK(std::find(names.begin(), names.end(), "color.g") != names.end());
    CHECK(std::find(names.begin(), names.end(), "transform.position.x") == names.end());
}

TEST_CASE("EditorCommands: 戻せるコマンドは履歴に積まれ、Undo で前の値に、Redo で新しい値になる。新しい変更でやり直しは消える")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));
    int calls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.count", calls)));

    // 何も無いうちは、戻せない(理由つきで失敗。落ちない)
    CHECK(commands.WhyCannotUndo(app) != nullptr);
    CHECK_FALSE(commands.Execute("editor.undo", app).succeeded);

    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": 10})")).succeeded);
    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": 20})")).succeeded);
    // 戻せないコマンド(undo を書かない)や、失敗したコマンドは、履歴に入らない
    CHECK(commands.Execute("test.count", app).succeeded);
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": "x"})")).succeeded);
    CHECK(commands.GetUndoCount() == 2);
    CHECK(std::string(commands.PeekUndoLabel()) == "Set Player (Actor).transform.position.x = 20");

    // 名前を変えても、履歴は guid で指しているので、同じアクターを戻せる
    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "name", "value": "Hero"})")).succeeded);
    CHECK(commands.Execute("editor.undo", app).succeeded);
    CHECK(actors.player->name == "Player");

    EditorCommandResult result = commands.Execute("editor.undo", app);
    CHECK(result.succeeded);
    CHECK(result.message == "Undo: Set Player (Actor).transform.position.x = 20");
    CHECK(actors.player->transform.position.x == 10.0f);
    CHECK(commands.Execute("editor.undo", app).succeeded);
    CHECK(actors.player->transform.position.x == 0.0f);
    CHECK(commands.GetUndoCount() == 0);
    CHECK(commands.GetRedoCount() == 3);

    // やり直すと、新しい値になる。Undo / Redo の実行そのものは、新しい変更として履歴に積まれない
    CHECK(commands.Execute("editor.redo", app).succeeded);
    CHECK(actors.player->transform.position.x == 10.0f);
    CHECK(commands.GetUndoCount() == 1);
    CHECK(commands.GetRedoCount() == 2);

    // 新しい変更をすると、やり直しの履歴は消える
    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.y", "value": 5})")).succeeded);
    CHECK(commands.GetRedoCount() == 0);
    CHECK(commands.WhyCannotRedo(app) != nullptr);

    // 相手のアクターが消えていたら、戻せない。その1件は捨てる(ずっと詰まらないように)
    actors.player->Destroy();
    app.GetScene().RemoveDestroyedActors();
    result = commands.Execute("editor.undo", app);
    CHECK_FALSE(result.succeeded);
    CHECK(commands.GetUndoCount() == 1);
}

TEST_CASE("EditorCommands: 戻すコマンドが今は使えない(再生中など)なら、Undo は灰色になり、履歴はそのまま残る")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    EditorCommandRegistry commands;
    bool editing = true;
    EditorCommand setProperty = husky::tools::MakeSetPropertyCommand();
    setProperty.whyUnavailable = [&editing](Application&) { return editing ? nullptr : "Stop playing first"; };
    REQUIRE(commands.Register(std::move(setProperty)));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));

    CHECK(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "transform.position.x", "value": 10})")).succeeded);
    editing = false;
    CHECK(std::string(commands.WhyCannotUndo(app)) == "Stop playing first");
    CHECK_FALSE(commands.Execute("editor.undo", app).succeeded);
    CHECK(commands.GetUndoCount() == 1);
    CHECK(actors.player->transform.position.x == 10.0f);

    editing = true;
    CHECK(commands.Execute("editor.undo", app).succeeded);
    CHECK(actors.player->transform.position.x == 0.0f);
}

TEST_CASE("DebugWindow: 種類を問わない引数は、打った文字を数や bool として読めればその値、読めなければ文字列で渡す")
{
    int calls = 0;
    EditorCommand command = MakeCountingCommand("test.any", calls);
    command.args = {{"value", husky::tools::EditorArgType::Any, "anything", true}};
    std::vector<husky::tools::CommandArgInput> inputs(1);

    inputs[0].text = "500";
    CHECK(husky::tools::DebugWindow::BuildArgs(command, inputs).Find("value")->AsInt() == 500);
    inputs[0].text = "true";
    CHECK(husky::tools::DebugWindow::BuildArgs(command, inputs).Find("value")->AsBool());
    inputs[0].text = "Player"; // 引用符が無くても文字列
    CHECK(husky::tools::DebugWindow::BuildArgs(command, inputs).Find("value")->AsString() == "Player");
    inputs[0].text = "\"500\""; // 引用符を付ければ、数の形でも文字列
    CHECK(husky::tools::DebugWindow::BuildArgs(command, inputs).Find("value")->AsString() == "500");
}

TEST_CASE("DebugWindow: コマンドのボタンと最後の結果を並べても、ImGui の使い方の誤りが無い。並べるだけでは実行しない")
{
    husky::tests::HeadlessImGui imgui;
    husky::tools::DebugWindow window; // 入力欄の値やボタンの文字の入れ物を持つので、フレームをまたいで使う
    Application app;
    EditorCommandRegistry commands;
    int calls = 0;
    REQUIRE(commands.Register(MakeCountingCommand("test.count", calls, Key::F5)));

    imgui.Frame([&] { window.Draw(app, &commands); }); // 結果がまだ無いとき
    commands.Execute("test.count", app);
    imgui.Frame([&] { window.Draw(app, &commands); }); // 結果があるとき
    imgui.Frame([&] { window.Draw(app, nullptr); });   // コマンド無し
    CHECK(calls == 1);
    CHECK(std::string(husky::tools::GetKeyDisplayName(Key::F5)) == "F5");
}


TEST_CASE("EditorCommands: prefab.overrides はプレハブとの違いを並べ、prefab.revert は1つか全部(名前・位置・回転を除く)をプレハブの値に戻す。Undo できる")
{
    husky::tests::TestFolder folder("husky_test_prefab_revert");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeSetManyPropertiesCommand()));
    REQUIRE(commands.Register(husky::tools::MakeListOverridesCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRevertPrefabCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));

    // 幅 24 の白いスプライトのプレハブを作って置き、3つの値を変える
    husky::engine::Actor* original = app.GetScene().Spawn<husky::engine::Actor>();
    original->name = "Coin";
    original->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    REQUIRE(commands.Execute("prefab.create", app, Json(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})")).succeeded);
    const EditorCommandResult spawned =
        commands.Execute("actor.spawn", app, Json(R"({"prefab": "prefabs/coin.prefab", "x": 0, "y": 0})"));
    REQUIRE(spawned.succeeded);
    const std::string guid = spawned.data.Find("guid")->AsString();
    husky::core::Guid parsed;
    REQUIRE(husky::core::Guid::Parse(guid, parsed));
    husky::engine::Actor* placed = app.GetScene().FindActorByGuid(parsed);
    REQUIRE(placed != nullptr);
    husky::engine::SpriteComponent* sprite = placed->GetComponent<husky::engine::SpriteComponent>();
    REQUIRE(sprite != nullptr);
    auto args = [&guid](const std::string& rest) { return Json(("{\"actor\": \"" + guid + "\"" + rest + "}").c_str()); };
    REQUIRE(commands.Execute("property.set", app, args(R"(, "property": "transform.position.x", "value": 5)")).succeeded);
    REQUIRE(commands.Execute("property.set", app, args(R"(, "component": "SpriteComponent", "property": "width", "value": 30)")).succeeded);
    REQUIRE(commands.Execute("property.set", app, args(R"(, "component": "SpriteComponent", "property": "color.r", "value": 0.5)")).succeeded);

    // --- prefab.overrides: 3つ。位置は「1体ずつ違う値」 ---
    const EditorCommandResult listed = commands.Execute("prefab.overrides", app, args(""));
    REQUIRE(listed.succeeded);
    REQUIRE(listed.data.GetArraySize() == 3);
    CHECK(listed.data.At(0).Find("property")->AsString() == "transform.position.x");
    CHECK(listed.data.At(0).Find("perInstance")->AsBool());
    CHECK(listed.data.At(1).Find("component")->AsString() == "SpriteComponent");
    CHECK(listed.data.At(1).Find("property")->AsString() == "width");
    CHECK(listed.data.At(1).Find("prefabValue")->AsFloat() == 24.0);
    CHECK_FALSE(commands.Execute("prefab.overrides", app, Json(R"({"actor": "Coin"})")).succeeded); // 名前が2体に重なる

    // --- 1つだけ戻す ---
    REQUIRE(commands.Execute("prefab.revert", app, args(R"(, "component": "SpriteComponent", "property": "width")")).succeeded);
    CHECK(sprite->width == 24.0f);
    CHECK(sprite->color.r == 0.5f);
    CHECK_FALSE(commands.Execute("prefab.revert", app, args(R"(, "component": "SpriteComponent", "property": "width")")).succeeded); // もう違わない

    // --- 全部を戻す: 色は戻るが、位置(1体ずつ違う値)は残る ---
    const EditorCommandResult reverted = commands.Execute("prefab.revert", app, args(""));
    REQUIRE(reverted.succeeded);
    CHECK(reverted.message.find("Reverted 1") == 0);
    CHECK(sprite->color.r == 1.0f);
    CHECK(placed->transform.position.x == 5.0f);
    CHECK(commands.Execute("prefab.revert", app, args("")).message.find("Nothing") == 0);

    // --- Undo / Redo: 全部を戻したのを戻す → 色が 0.5 に。もう1回で幅が 30 に。やり直すと幅は 24 ---
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(sprite->color.r == 0.5f);
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(sprite->width == 30.0f);
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(sprite->width == 24.0f);
}

TEST_CASE("EditorCommands: actor.destroy は消す前に控え、Undo(actor.restore)で同じ guid・プレハブとの違いのまま作り直す")
{
    husky::tests::TestFolder folder("husky_test_actor_destroy");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeDestroyActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRestoreActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));

    // プレハブから置き、幅だけを変える(プレハブとの違いが1つある)
    husky::engine::Actor* original = app.GetScene().Spawn<husky::engine::Actor>();
    original->name = "Coin";
    original->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    REQUIRE(commands.Execute("prefab.create", app, Json(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})")).succeeded);
    const EditorCommandResult spawned =
        commands.Execute("actor.spawn", app, Json(R"({"prefab": "prefabs/coin.prefab", "x": 7, "y": 0})"));
    REQUIRE(spawned.succeeded);
    const std::string guidText = spawned.data.Find("guid")->AsString();
    husky::core::Guid guid;
    REQUIRE(husky::core::Guid::Parse(guidText, guid));
    const std::string actorArg = "{\"actor\": \"" + guidText + "\"}";
    REQUIRE(commands.Execute("property.set", app, Json(("{\"actor\": \"" + guidText +
                                                        R"(", "component": "SpriteComponent", "property": "width", "value": 30})")
                                                           .c_str()))
                .succeeded);

    // --- 消す: Destroy の印が付き、guid では見つからなくなる。フレームの終わりで一覧から外れる ---
    husky::engine::Actor* placed = app.GetScene().FindActorByGuid(guid);
    REQUIRE(placed != nullptr);
    REQUIRE(commands.Execute("actor.destroy", app, Json(actorArg.c_str())).succeeded);
    CHECK(placed->IsPendingDestroy());
    CHECK(app.GetScene().FindActorByGuid(guid) == nullptr);
    CHECK_FALSE(commands.Execute("actor.destroy", app, Json(actorArg.c_str())).succeeded); // もういない
    app.GetScene().RemoveDestroyedActors();
    app.GetRegistry().FlushDestroyed();

    // --- Undo: 同じ guid で戻る。プレハブとのつながり、違い(幅 30)、プレハブの値、位置もそのまま ---
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    husky::engine::Actor* restored = app.GetScene().FindActorByGuid(guid);
    REQUIRE(restored != nullptr);
    CHECK(restored->name == "Coin");
    CHECK(restored->transform.position.x == 7.0f);
    CHECK_FALSE(restored->prefab.guid.IsNull());
    CHECK_FALSE(restored->prefab.asset.IsNull());
    husky::engine::SpriteComponent* sprite = restored->GetComponent<husky::engine::SpriteComponent>();
    REQUIRE(sprite != nullptr);
    CHECK(sprite->width == 30.0f);
    CHECK(sprite->GetOwner() == restored);

    // 生きているものと同じ guid では戻せない(guid が振り直されて履歴と食い違うので、先に断る)
    const EditorCommandResult twice = commands.Execute("actor.destroy", app, Json(actorArg.c_str()));
    REQUIRE(twice.succeeded);
    REQUIRE(commands.Execute("actor.restore", app, twice.undo.args).succeeded);
    CHECK_FALSE(commands.Execute("actor.restore", app, twice.undo.args).succeeded);
    CHECK_FALSE(commands.Execute("actor.restore", app, Json(R"({"data": {"objects": [], "actors": []}})")).succeeded);

    // --- Redo / Undo を続けても、同じ guid のアクターが消えたり戻ったりする ---
    REQUIRE(commands.Execute("editor.undo", app).succeeded); // actor.restore を戻す = 消す
    CHECK(app.GetScene().FindActorByGuid(guid) == nullptr);
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(app.GetScene().FindActorByGuid(guid) != nullptr);
}

TEST_CASE("EditorCommands: actor.spawn は Undo で消え、Redo で同じ guid のまま戻る(後の履歴の guid と食い違わない)")
{
    Application app;
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeDestroyActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRestoreActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));

    const EditorCommandResult spawned = commands.Execute("actor.spawn", app, Json(R"({"type": "Actor", "x": 3, "y": 4})"));
    REQUIRE(spawned.succeeded);
    const std::string guidText = spawned.data.Find("guid")->AsString();
    husky::core::Guid guid;
    REQUIRE(husky::core::Guid::Parse(guidText, guid));
    // 置いた後に名前を変える(この履歴は guid で指す)
    REQUIRE(commands.Execute("property.set", app, Json(("{\"actor\": \"" + guidText +
                                                        R"(", "property": "name", "value": "Box"})")
                                                           .c_str()))
                .succeeded);
    CHECK(commands.GetUndoCount() == 2);

    // 名前を戻し、置いたのも戻す → いなくなる
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(app.GetScene().FindActorByGuid(guid) == nullptr);
    app.GetScene().RemoveDestroyedActors();
    app.GetRegistry().FlushDestroyed();

    // やり直す → 同じ guid・同じ位置で戻り、続く名前の変更もやり直せる
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    husky::engine::Actor* back = app.GetScene().FindActorByGuid(guid);
    REQUIRE(back != nullptr);
    CHECK(back->transform.position.x == 3.0f);
    CHECK(back->transform.position.y == 4.0f);
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(back->name == "Box");
}

TEST_CASE("EditorCommands: property.setMany はまとめて書き換え、1つでも書けなければ何も変えない")
{
    Application app;
    const NamedActors actors = SpawnNamedActors(app);
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetManyPropertiesCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));

    const EditorCommandResult result = commands.Execute("property.setMany", app, Json(R"({"actor": "Player", "values": [
        {"property": "transform.position.x", "value": 1}, {"property": "name", "value": "Hero"}]})"));
    REQUIRE(result.succeeded);
    CHECK(actors.player->transform.position.x == 1.0f);
    CHECK(actors.player->name == "Hero");

    // 2つ目が書けない(種類が違う)ので、1つ目も元に戻る
    CHECK_FALSE(commands.Execute("property.setMany", app, Json(R"({"actor": "Hero", "values": [
        {"property": "transform.position.x", "value": 7}, {"property": "name", "value": 3}]})")).succeeded);
    CHECK(actors.player->transform.position.x == 1.0f);

    // Undo で、まとめて前の値に戻る
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(actors.player->transform.position.x == 0.0f);
    CHECK(actors.player->name == "Player");
}

TEST_CASE("EditorCommands: prefab.apply は違いをプレハブのファイルに書き戻し、同じプレハブから置いた全員に伝える。自分で変えた値はそのまま")
{
    husky::tests::TestFolder folder("husky_test_prefab_apply");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeListOverridesCommand()));
    REQUIRE(commands.Register(husky::tools::MakeApplyPrefabCommand(folder.Root())));

    husky::engine::Actor* original = app.GetScene().Spawn<husky::engine::Actor>();
    original->name = "Coin";
    original->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    REQUIRE(commands.Execute("prefab.create", app, Json(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})")).succeeded);

    // 3つ置く。a は幅・色・位置を、b は色だけを自分で変える。c は何も変えない
    auto place = [&]() {
        const EditorCommandResult spawned =
            commands.Execute("actor.spawn", app, Json(R"({"prefab": "prefabs/coin.prefab", "x": 0, "y": 0})"));
        REQUIRE(spawned.succeeded);
        return spawned.data.Find("guid")->AsString();
    };
    const std::string a = place();
    const std::string b = place();
    const std::string c = place();
    auto sprite = [&app](const std::string& guidText) {
        husky::core::Guid guid;
        REQUIRE(husky::core::Guid::Parse(guidText, guid));
        husky::engine::Actor* actor = app.GetScene().FindActorByGuid(guid);
        REQUIRE(actor != nullptr);
        return actor->GetComponent<husky::engine::SpriteComponent>();
    };
    auto args = [](const std::string& guid, const std::string& rest) {
        return Json(("{\"actor\": \"" + guid + "\"" + rest + "}").c_str());
    };
    REQUIRE(commands.Execute("property.set", app, args(a, R"(, "component": "SpriteComponent", "property": "width", "value": 40)")).succeeded);
    REQUIRE(commands.Execute("property.set", app, args(a, R"(, "component": "SpriteComponent", "property": "color.r", "value": 0.5)")).succeeded);
    REQUIRE(commands.Execute("property.set", app, args(a, R"(, "property": "transform.position.x", "value": 9)")).succeeded);
    REQUIRE(commands.Execute("property.set", app, args(b, R"(, "component": "SpriteComponent", "property": "color.r", "value": 0.2)")).succeeded);

    // --- 1つだけ書き戻す(幅): b と c にも伝わる ---
    const EditorCommandResult width = commands.Execute("prefab.apply", app, args(a, R"(, "component": "SpriteComponent", "property": "width")"));
    REQUIRE(width.succeeded);
    CHECK(width.data.Find("followed")->AsInt() == 2);
    CHECK(sprite(b)->width == 40.0f);
    CHECK(sprite(c)->width == 40.0f);
    std::string text;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("prefabs/coin.prefab").c_str(), text));
    ArchiveValue file;
    REQUIRE(husky::engine::JsonFormat::Read(text, file));
    CHECK(file.Find("objects")->At(1).Find("properties")->Find("width")->AsFloat() == 40.0);

    // --- 全部を書き戻す: 色は c に伝わり、自分で変えた b はそのまま。位置(1体ずつ違う値)は書き戻さない ---
    REQUIRE(commands.Execute("prefab.apply", app, args(a, "")).succeeded);
    CHECK(sprite(c)->color.r == 0.5f);
    CHECK(sprite(b)->color.r == 0.2f);
    const EditorCommandResult left = commands.Execute("prefab.overrides", app, args(a, ""));
    REQUIRE(left.succeeded);
    REQUIRE(left.data.GetArraySize() == 1); // 残った違いは位置だけ
    CHECK(left.data.At(0).Find("property")->AsString() == "transform.position.x");

    // 新しく置いたものは、新しいプレハブの値になる
    const std::string d = place();
    CHECK(sprite(d)->width == 40.0f);
    CHECK(sprite(d)->color.r == 0.5f);
}

namespace
{
    // PF-7c のテスト用: component.add / component.remove と Undo / Redo を登録する。
    void RegisterComponentCommands(EditorCommandRegistry& commands)
    {
        REQUIRE(commands.Register(husky::tools::MakeAddComponentCommand()));
        REQUIRE(commands.Register(husky::tools::MakeRemoveComponentCommand()));
        REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
        REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));
    }

    // index 番目のコンポーネントの型の名前(無ければ空)。
    std::string ComponentTypeAt(Application& app, const husky::engine::Actor& actor, std::size_t index)
    {
        if (index >= actor.components.size())
        {
            return std::string();
        }
        const husky::engine::Component* component = app.GetRegistry().Resolve<husky::engine::Component>(actor.components[index]);
        return component != nullptr ? component->GetType().GetName() : std::string();
    }
}

TEST_CASE("PF-7c: component.add は指定の位置に足し、Undo で外れ、Redo で同じ位置に戻る")
{
    Application app;
    EditorCommandRegistry commands;
    RegisterComponentCommands(commands);
    husky::engine::Actor* hero = app.GetScene().Spawn<husky::engine::Actor>();
    hero->name = "Hero";
    hero->AddComponent<husky::engine::SpriteComponent>();
    hero->AddComponent<husky::engine::CameraComponent>();

    const EditorCommandResult added =
        commands.Execute("component.add", app, Json(R"({"actor": "Hero", "type": "SpriteAnimatorComponent", "index": 1})"));
    REQUIRE(added.succeeded);
    CHECK(added.data.Find("index")->AsInt() == 1);
    REQUIRE(hero->components.size() == 3);
    CHECK(ComponentTypeAt(app, *hero, 1) == "SpriteAnimatorComponent");
    REQUIRE(hero->GetComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
    CHECK(hero->GetComponent<husky::engine::SpriteAnimatorComponent>()->GetOwner() == hero);

    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(hero->components.size() == 2);
    CHECK(hero->GetComponent<husky::engine::SpriteAnimatorComponent>() == nullptr);
    CHECK(ComponentTypeAt(app, *hero, 1) == "CameraComponent");
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(ComponentTypeAt(app, *hero, 1) == "SpriteAnimatorComponent");

    // 省くと末尾。知らない型、コンポーネントでない型、負の位置は断る
    REQUIRE(commands.Execute("component.add", app, Json(R"({"actor": "Hero", "type": "CameraComponent"})")).succeeded);
    CHECK(ComponentTypeAt(app, *hero, 3) == "CameraComponent");
    CHECK_FALSE(commands.Execute("component.add", app, Json(R"({"actor": "Hero", "type": "NoSuchType"})")).succeeded);
    CHECK_FALSE(commands.Execute("component.add", app, Json(R"({"actor": "Hero", "type": "Actor"})")).succeeded);
    CHECK_FALSE(commands.Execute("component.add", app, Json(R"({"actor": "Hero", "type": "CameraComponent", "index": -1})")).succeeded);
}

TEST_CASE("PF-7c: component.remove はすぐに一覧から外し、Undo で同じ位置・同じ値で戻る(同じ型が2つあっても位置で取り違えない)")
{
    Application app;
    EditorCommandRegistry commands;
    RegisterComponentCommands(commands);
    husky::engine::Actor* hero = app.GetScene().Spawn<husky::engine::Actor>();
    hero->name = "Hero";
    hero->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    hero->AddComponent<husky::engine::CameraComponent>()->zoom = 2.0f;
    hero->AddComponent<husky::engine::CameraComponent>()->zoom = 3.0f;

    // 型で指すと最初のもの(zoom 2)。外したものには Destroy の印
    husky::engine::CameraComponent* first = hero->GetComponent<husky::engine::CameraComponent>();
    REQUIRE(commands.Execute("component.remove", app, Json(R"({"actor": "Hero", "component": "CameraComponent"})")).succeeded);
    CHECK(first->IsPendingDestroy());
    REQUIRE(hero->components.size() == 2);
    CHECK(hero->GetComponent<husky::engine::CameraComponent>()->zoom == 3.0f);
    app.GetScene().RemoveDestroyedActors();
    app.GetRegistry().FlushDestroyed();

    // Undo: 1番目に、zoom 2 のまま戻る(新しいオブジェクトとして)
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    REQUIRE(hero->components.size() == 3);
    CHECK(ComponentTypeAt(app, *hero, 1) == "CameraComponent");
    husky::engine::CameraComponent* back = app.GetRegistry().Resolve<husky::engine::CameraComponent>(hero->components[1]);
    REQUIRE(back != nullptr);
    CHECK(back->zoom == 2.0f);
    CHECK(back->GetOwner() == hero);
    // Redo: 位置で指すので、また zoom 2 のほうが外れる
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    REQUIRE(hero->components.size() == 2);
    CHECK(hero->GetComponent<husky::engine::CameraComponent>()->zoom == 3.0f);

    // 位置と型が合わない、付いていない型は断る
    CHECK_FALSE(commands.Execute("component.remove", app, Json(R"({"actor": "Hero", "component": "CameraComponent", "index": 0})")).succeeded);
    CHECK_FALSE(commands.Execute("component.remove", app, Json(R"({"actor": "Hero", "component": "SpriteAnimatorComponent"})")).succeeded);
    CHECK(hero->components.size() == 2);
}

TEST_CASE("PF-7c: プレハブから来たコンポーネントを外すと removedComponents に入り、Undo で同じ ID に戻ると違いではなくなる")
{
    husky::tests::TestFolder folder("husky_test_component_remove_prefab");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    RegisterComponentCommands(commands);

    husky::engine::Actor* original = app.GetScene().Spawn<husky::engine::Actor>();
    original->name = "Coin";
    original->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    REQUIRE(commands.Execute("prefab.create", app, Json(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})")).succeeded);
    original->Destroy();
    app.GetScene().RemoveDestroyedActors();
    const EditorCommandResult spawned = commands.Execute("actor.spawn", app, Json(R"({"prefab": "prefabs/coin.prefab"})"));
    REQUIRE(spawned.succeeded);
    const std::string guidText = spawned.data.Find("guid")->AsString();
    husky::core::Guid guid;
    REQUIRE(husky::core::Guid::Parse(guidText, guid));
    husky::engine::Actor* placed = app.GetScene().FindActorByGuid(guid);
    REQUIRE(placed != nullptr);
    REQUIRE(placed->GetComponent<husky::engine::SpriteComponent>() != nullptr);
    const husky::core::Guid componentId = placed->GetComponent<husky::engine::SpriteComponent>()->prefabComponentId;
    REQUIRE_FALSE(componentId.IsNull());

    // 保存したアクターの記録(objects の中で guid が合うもの)に removedComponents があるか
    auto hasRemoved = [&app, &guidText]() {
        const ArchiveValue written = husky::engine::SceneSerializer::Write(app.GetScene()).value;
        const ArchiveValue* objects = written.Find("objects");
        for (std::size_t i = 0; objects != nullptr && i < objects->GetArraySize(); ++i)
        {
            const ArchiveValue* properties = objects->At(i).Find("properties");
            const ArchiveValue* guidValue = properties != nullptr ? properties->Find("guid") : nullptr;
            if (guidValue != nullptr && guidValue->AsString() == guidText)
            {
                return objects->At(i).Find("removedComponents") != nullptr;
            }
        }
        return false;
    };

    const std::string removeArgs = "{\"actor\": \"" + guidText + "\", \"component\": \"SpriteComponent\"}";
    REQUIRE(commands.Execute("component.remove", app, Json(removeArgs.c_str())).succeeded);
    CHECK(placed->GetComponent<husky::engine::SpriteComponent>() == nullptr);
    CHECK(hasRemoved());
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    husky::engine::SpriteComponent* back = placed->GetComponent<husky::engine::SpriteComponent>();
    REQUIRE(back != nullptr);
    CHECK(back->prefabComponentId == componentId);
    CHECK(back->width == 24.0f);
    CHECK_FALSE(hasRemoved());
}

TEST_CASE("PF-7c: 外したコンポーネントのアクターへの参照は guid で控え、相手が作り直されていても Undo でつながり直る")
{
    Application app;
    app.GetTypeRegistry().Register<husky::game::CameraFollowComponent>();
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeDestroyActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRestoreActorCommand()));
    RegisterComponentCommands(commands);
    husky::engine::Actor* player = app.GetScene().Spawn<husky::engine::Actor>();
    player->name = "Player";
    husky::engine::Actor* camera = app.GetScene().Spawn<husky::engine::Actor>();
    camera->name = "MainCamera";
    camera->AddComponent<husky::game::CameraFollowComponent>()->target = player->GetHandle();
    const husky::core::Guid playerGuid = player->guid;

    // 追従を外し、それから相手を消して戻す(戻した相手は新しいオブジェクトで、ハンドルは変わる。guid は同じ)
    const EditorCommandResult removed =
        commands.Execute("component.remove", app, Json(R"({"actor": "MainCamera", "component": "CameraFollowComponent"})"));
    REQUIRE(removed.succeeded);
    CHECK(removed.details.empty());
    REQUIRE(commands.Execute("actor.destroy", app, Json(R"({"actor": "Player"})")).succeeded);
    app.GetScene().RemoveDestroyedActors();
    app.GetRegistry().FlushDestroyed();
    REQUIRE(commands.Execute("editor.undo", app).succeeded); // プレイヤーが戻る
    REQUIRE(commands.Execute("editor.undo", app).succeeded); // 追従が戻る
    husky::game::CameraFollowComponent* follow = camera->GetComponent<husky::game::CameraFollowComponent>();
    REQUIRE(follow != nullptr);
    husky::engine::Actor* restoredPlayer = app.GetScene().FindActorByGuid(playerGuid);
    REQUIRE(restoredPlayer != nullptr);
    CHECK(follow->target == restoredPlayer->GetHandle());
}

TEST_CASE("PF-7d: prefab.revertComponent は外したコンポーネントをプレハブの値と ID で、プレハブと同じ並びの位置に戻す")
{
    husky::tests::TestFolder folder("husky_test_revert_component");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRevertRemovedComponentCommand()));
    RegisterComponentCommands(commands);

    // プレハブ: スプライト(幅 24)、アニメーター、カメラ(zoom 2)の順
    husky::engine::Actor* original = app.GetScene().Spawn<husky::engine::Actor>();
    original->name = "Coin";
    original->AddComponent<husky::engine::SpriteComponent>()->width = 24.0f;
    original->AddComponent<husky::engine::SpriteAnimatorComponent>();
    original->AddComponent<husky::engine::CameraComponent>()->zoom = 2.0f;
    REQUIRE(commands.Execute("prefab.create", app, Json(R"({"actor": "Coin", "path": "prefabs/coin.prefab"})")).succeeded);
    original->Destroy();
    app.GetScene().RemoveDestroyedActors();
    const EditorCommandResult spawned = commands.Execute("actor.spawn", app, Json(R"({"prefab": "prefabs/coin.prefab"})"));
    REQUIRE(spawned.succeeded);
    const std::string guidText = spawned.data.Find("guid")->AsString();
    husky::core::Guid guid;
    REQUIRE(husky::core::Guid::Parse(guidText, guid));
    husky::engine::Actor* placed = app.GetScene().FindActorByGuid(guid);
    REQUIRE(placed != nullptr);
    REQUIRE(placed->GetComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
    const husky::core::Guid animatorId = placed->GetComponent<husky::engine::SpriteAnimatorComponent>()->prefabComponentId;

    // 真ん中(アニメーター)を外し、カメラを1つ足す。違いの一覧: 外したものは戻す位置 1(スプライトの後ろ)、足したものは位置 2
    const std::string removeArgs = "{\"actor\": \"" + guidText + "\", \"component\": \"SpriteAnimatorComponent\"}";
    REQUIRE(commands.Execute("component.remove", app, Json(removeArgs.c_str())).succeeded);
    const std::string addArgs = "{\"actor\": \"" + guidText + "\", \"type\": \"CameraComponent\"}";
    REQUIRE(commands.Execute("component.add", app, Json(addArgs.c_str())).succeeded);
    {
        const husky::engine::Prefab::OverridesResult found = husky::engine::Prefab::FindOverrides(*placed);
        REQUIRE(found.removedComponents.size() == 1);
        CHECK(found.removedComponents[0].type == "SpriteAnimatorComponent");
        CHECK(found.removedComponents[0].componentId == animatorId);
        CHECK(found.removedComponents[0].index == 1);
        REQUIRE(found.addedComponents.size() == 1);
        CHECK(found.addedComponents[0].index == 2);
    }

    // 戻す: 位置 1 に、同じ ID で。もう外したものは無い(足したものは残る)
    const std::string revertArgs = "{\"actor\": \"" + guidText + "\", \"componentId\": \"" + animatorId.ToString() + "\"}";
    const EditorCommandResult reverted = commands.Execute("prefab.revertComponent", app, Json(revertArgs.c_str()));
    REQUIRE(reverted.succeeded);
    CHECK(ComponentTypeAt(app, *placed, 1) == "SpriteAnimatorComponent");
    CHECK(placed->GetComponent<husky::engine::SpriteAnimatorComponent>()->prefabComponentId == animatorId);
    CHECK(placed->GetComponent<husky::engine::SpriteAnimatorComponent>()->GetOwner() == placed);
    CHECK(husky::engine::Prefab::FindOverrides(*placed).removedComponents.empty());
    CHECK(husky::engine::Prefab::FindOverrides(*placed).addedComponents.size() == 1);
    // 2回目は、もう外したものではないので断る。ID でないもの、知らない ID も断る
    CHECK_FALSE(commands.Execute("prefab.revertComponent", app, Json(revertArgs.c_str())).succeeded);
    const std::string badArgs = "{\"actor\": \"" + guidText + "\", \"componentId\": \"nope\"}";
    CHECK_FALSE(commands.Execute("prefab.revertComponent", app, Json(badArgs.c_str())).succeeded);

    // Undo: また外れる。Redo: また戻る
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(placed->GetComponent<husky::engine::SpriteAnimatorComponent>() == nullptr);
    CHECK(husky::engine::Prefab::FindOverrides(*placed).removedComponents.size() == 1);
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(ComponentTypeAt(app, *placed, 1) == "SpriteAnimatorComponent");

    // 先頭(スプライト)を外して戻すと、位置 0 に、プレハブの値(幅 24)で戻る(外す前に変えた値は戻らない)
    const std::string spriteSet = "{\"actor\": \"" + guidText + "\", \"component\": \"SpriteComponent\"}";
    placed->GetComponent<husky::engine::SpriteComponent>()->width = 99.0f;
    const husky::core::Guid spriteId = placed->GetComponent<husky::engine::SpriteComponent>()->prefabComponentId;
    REQUIRE(commands.Execute("component.remove", app, Json(spriteSet.c_str())).succeeded);
    const std::string revertSprite = "{\"actor\": \"" + guidText + "\", \"componentId\": \"" + spriteId.ToString() + "\"}";
    REQUIRE(commands.Execute("prefab.revertComponent", app, Json(revertSprite.c_str())).succeeded);
    CHECK(ComponentTypeAt(app, *placed, 0) == "SpriteComponent");
    CHECK(placed->GetComponent<husky::engine::SpriteComponent>()->width == 24.0f);
    CHECK(husky::engine::Prefab::FindOverrides(*placed).overrides.empty());
}
#endif
