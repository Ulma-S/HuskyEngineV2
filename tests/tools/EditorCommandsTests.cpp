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
    for (const husky::tools::EditorArgChoice& choice : commands.Find("actor.spawn")->args[0].choices(app, ArchiveValue()))
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
        actors.enemyB->x = 50.0f;
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

    EditorCommandResult result = commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "x", "value": 120})"));
    CHECK(result.succeeded);
    CHECK(actors.player->x == 120.0f);
    CHECK(result.message == "Set Player (Actor).x = 120");
    REQUIRE_FALSE(result.details.empty());
    CHECK(result.details[0] == "was 0.0");

    // 名前が重なっていると、名前では指せない(2体いる)
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Enemy", "property": "x", "value": 1})")).succeeded);
    CHECK(actors.enemyA->x == 0.0f);

    // 名前が重なっていても、guid なら指せる
    const std::string args = std::string(R"({"actor": ")") + actors.enemyB->guid.ToString() +
                             R"(", "property": "name", "value": "Boss"})";
    CHECK(commands.Execute("property.set", app, Json(args.c_str())).succeeded);
    CHECK(actors.enemyB->name == "Boss");
    CHECK(actors.enemyA->name == "Enemy"); // もう1体は変わらない

    // 断るもの(どれも値は変えない)
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "x", "value": "left"})")).succeeded);
    CHECK(actors.player->x == 120.0f);
    const husky::core::Guid guidBefore = actors.player->guid;
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "guid", "value": "x"})")).succeeded);
    CHECK(actors.player->guid == guidBefore);
    CHECK_FALSE(commands.Execute("property.set", app, Json(R"({"actor": "Player", "property": "speed", "value": 1})")).succeeded);

    // プロパティの選択肢は、選んだアクターの書き換えられるプロパティ(guid と components は入らない)
    std::vector<std::string> names;
    for (const husky::tools::EditorArgChoice& choice :
         commands.Find("property.set")->args[1].choices(app, Json(R"({"actor": "Player"})")))
    {
        names.push_back(choice.value);
    }
    CHECK(std::find(names.begin(), names.end(), "x") != names.end());
    CHECK(std::find(names.begin(), names.end(), "guid") == names.end());
    CHECK(std::find(names.begin(), names.end(), "components") == names.end());

    // アクターの選択肢は、表示が人向け(名前と型)で、値が guid
    const std::vector<husky::tools::EditorArgChoice> actorChoices =
        commands.Find("property.set")->args[0].choices(app, ArchiveValue());
    REQUIRE(actorChoices.size() == 3);
    CHECK(actorChoices[2].value == actors.player->guid.ToString());
    CHECK(actorChoices[2].label == "Player (Actor)");
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

#endif
