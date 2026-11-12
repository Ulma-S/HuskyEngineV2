// エディタの CLI(RunEditorCli)のテスト(開発ビルドだけ)。exe を起動せずに、中身の関数を直接呼ぶ。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCli.h"

#include "engine/ArchiveValue.h"
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::tools::RunEditorCli;
namespace CliExitCode = husky::tools::CliExitCode;

namespace
{
    // テスト用の assets: .meta のある画像と、.meta の無い画像。
    void WriteAssets(const husky::tests::TestFolder& folder)
    {
        folder.WriteAsset("textures/old.png", husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a71ull));
        folder.Write("textures/new.png", "dummy");
    }
}

TEST_CASE("EditorCli: --help は使い方を出して成功。コマンドが無い・知らないオプションは、使い方の誤り(終了コード 2)")
{
    std::string out;
    CHECK(RunEditorCli({"--help"}, "unused", out) == CliExitCode::Success);
    CHECK(out.find("Usage:") != std::string::npos);

    out.clear();
    CHECK(RunEditorCli({}, "unused", out) == CliExitCode::UsageError);
    CHECK(out.find("no command") != std::string::npos);

    out.clear();
    CHECK(RunEditorCli({"--nope"}, "unused", out) == CliExitCode::UsageError);
    CHECK(out.find("--nope") != std::string::npos);

    out.clear();
    CHECK(RunEditorCli({"--assets"}, "unused", out) == CliExitCode::UsageError); // 値が無い
}

TEST_CASE("EditorCli: --list はコマンドの名前と説明を出し、--json-out で JSON にも書く")
{
    husky::tests::TestFolder folder("husky_test_cli_list");
    std::string out;
    const std::string jsonPath = folder.PathOf("list.json");
    CHECK(RunEditorCli({"--list", "--json-out", jsonPath}, folder.Root(), out) == CliExitCode::Success);
    CHECK(out.find("asset.refresh") != std::string::npos);

    std::string text;
    REQUIRE(husky::platform::ReadTextFile(jsonPath.c_str(), text));
    husky::engine::ArchiveValue json;
    REQUIRE(husky::engine::JsonFormat::Read(text, json));
    const husky::engine::ArchiveValue* commands = json.Find("commands");
    REQUIRE(commands != nullptr);
    REQUIRE(commands->GetArraySize() >= 1);
    CHECK(commands->At(0).Find("name")->AsString() == "asset.refresh");
    CHECK_FALSE(commands->At(0).Find("description")->AsString().empty());
}

TEST_CASE("EditorCli: コマンドを名前で実行し、--assets のフォルダを相手にする。結果は画面と JSON の両方に出る")
{
    husky::tests::TestFolder folder("husky_test_cli_refresh");
    WriteAssets(folder);
    std::string out;
    const std::string jsonPath = folder.PathOf("result.json");
    CHECK(RunEditorCli({"asset.refresh", "--assets", folder.Root(), "--json-out", jsonPath}, "unused", out) ==
          CliExitCode::Success);
    CHECK(out.find("OK: 2 assets, created 1 .meta") != std::string::npos);
    CHECK(husky::platform::FileExists(folder.PathOf("textures/new.png.meta").c_str()));

    std::string text;
    REQUIRE(husky::platform::ReadTextFile(jsonPath.c_str(), text));
    husky::engine::ArchiveValue json;
    REQUIRE(husky::engine::JsonFormat::Read(text, json));
    CHECK(json.Find("command")->AsString() == "asset.refresh");
    CHECK(json.Find("succeeded")->AsBool());
    CHECK(json.Find("message")->AsString() == "2 assets, created 1 .meta");
    CHECK(json.Find("details")->GetArraySize() >= 1);
}

TEST_CASE("EditorCli: シーンを書き換えるコマンドは --scene のファイルを読んでから実行し、成功したら保存する(無ければ作る)")
{
    husky::tests::TestFolder folder("husky_test_cli_scene");
    const std::string scene = folder.PathOf("level.json");
    std::string out;

    // --scene が無ければ、使い方の誤り
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "Actor"})"}, folder.Root(), out) == CliExitCode::UsageError);
    CHECK(out.find("--scene") != std::string::npos);

    // 1回目: ファイルが無いので、空のシーンから作る
    out.clear();
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "Actor", "x": 10})", "--scene", scene}, folder.Root(), out) ==
          CliExitCode::Success);
    CHECK(out.find("created scene") != std::string::npos);
    CHECK(husky::platform::FileExists(scene.c_str()));

    // 2回目: 1回目のファイルを読んでから置くので、2体になる
    out.clear();
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "Actor", "x": 20})", "--scene", scene}, folder.Root(), out) ==
          CliExitCode::Success);
    CHECK(out.find("saved scene") != std::string::npos);
    husky::engine::Application check;
    REQUIRE(check.LoadSceneFile(scene));
    CHECK(check.GetScene().actors.size() == 2);

    // 引数の誤りは使い方の誤り(2)、コマンドの失敗(知らない型)は 1。どちらもファイルは変えない
    out.clear();
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "Actor", "x": "left"})", "--scene", scene}, folder.Root(), out) ==
          CliExitCode::UsageError);
    CHECK(out.find("'x' (float)") != std::string::npos);
    out.clear();
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "NoSuchActor"})", "--scene", scene}, folder.Root(), out) ==
          CliExitCode::CommandFailed);
    husky::engine::Application recheck;
    REQUIRE(recheck.LoadSceneFile(scene));
    CHECK(recheck.GetScene().actors.size() == 2);
}

TEST_CASE("EditorCli: 読むだけのコマンド(actor.list)は --scene を読んで保存しない。property.set は名前で指して保存する")
{
    husky::tests::TestFolder folder("husky_test_cli_list_set");
    const std::string scene = folder.PathOf("level.json");
    std::string out;

    // 読むだけのコマンドは、無いファイルを作らない
    CHECK(RunEditorCli({"actor.list", "--scene", scene}, folder.Root(), out) == CliExitCode::CommandFailed);
    CHECK_FALSE(husky::platform::FileExists(scene.c_str()));
    out.clear();
    CHECK(RunEditorCli({"actor.list"}, folder.Root(), out) == CliExitCode::UsageError); // --scene が無い

    // アクターを置いて名前を付け、名前で指して位置を変える
    REQUIRE(RunEditorCli({"actor.spawn", R"({"type": "Actor"})", "--scene", scene}, folder.Root(), out) == CliExitCode::Success);
    std::string listText;
    const std::string listJson = folder.PathOf("list.json");
    REQUIRE(RunEditorCli({"actor.list", "--scene", scene, "--json-out", listJson}, folder.Root(), out) == CliExitCode::Success);
    REQUIRE(husky::platform::ReadTextFile(listJson.c_str(), listText));
    husky::engine::ArchiveValue list;
    REQUIRE(husky::engine::JsonFormat::Read(listText, list));
    REQUIRE(list.Find("data") != nullptr);
    REQUIRE(list.Find("data")->GetArraySize() == 1);
    const std::string guid = list.Find("data")->At(0).Find("guid")->AsString();

    const std::string rename = R"({"actor": ")" + guid + R"(", "property": "name", "value": "Player"})";
    CHECK(RunEditorCli({"property.set", rename, "--scene", scene}, folder.Root(), out) == CliExitCode::Success);
    out.clear();
    CHECK(RunEditorCli({"property.set", R"({"actor": "Player", "property": "x", "value": 64})", "--scene", scene},
                       folder.Root(), out) == CliExitCode::Success);
    CHECK(out.find("was 0.0") != std::string::npos);

    husky::engine::Application check;
    REQUIRE(check.LoadSceneFile(scene));
    REQUIRE(check.GetScene().actors.size() == 1);
    const husky::engine::Actor* actor = check.GetRegistry().Resolve<husky::engine::Actor>(check.GetScene().actors[0]);
    REQUIRE(actor != nullptr);
    CHECK(actor->name == "Player");
    CHECK(actor->x == 64.0f);
    CHECK(actor->guid.ToString() == guid); // 保存して読み戻しても、同じ guid
}

TEST_CASE("EditorCli: ゲームの型は、渡した関数(setupApplication)で登録してから使える")
{
    husky::tests::TestFolder folder("husky_test_cli_setup");
    const std::string scene = folder.PathOf("level.json");
    std::string out;
    bool called = false;
    CHECK(RunEditorCli({"actor.spawn", R"({"type": "Actor"})", "--scene", scene}, folder.Root(), out,
                       [&called](husky::engine::Application&) { called = true; }) == CliExitCode::Success);
    CHECK(called);
}

TEST_CASE("EditorCli: --list は引数の形も出す")
{
    husky::tests::TestFolder folder("husky_test_cli_list_args");
    std::string out;
    const std::string jsonPath = folder.PathOf("list.json");
    CHECK(RunEditorCli({"--list", "--json-out", jsonPath}, folder.Root(), out) == CliExitCode::Success);
    CHECK(out.find("type (string, required)") != std::string::npos);

    std::string text;
    REQUIRE(husky::platform::ReadTextFile(jsonPath.c_str(), text));
    husky::engine::ArchiveValue json;
    REQUIRE(husky::engine::JsonFormat::Read(text, json));
    const husky::engine::ArchiveValue* commands = json.Find("commands");
    REQUIRE(commands != nullptr);
    bool found = false;
    for (std::size_t i = 0; i < commands->GetArraySize(); ++i)
    {
        const husky::engine::ArchiveValue& command = commands->At(i);
        if (command.Find("name")->AsString() == "actor.spawn")
        {
            found = true;
            CHECK(command.Find("modifiesScene")->AsBool());
            REQUIRE(command.Find("args")->GetArraySize() == 3);
            CHECK(command.Find("args")->At(0).Find("name")->AsString() == "type");
            CHECK(command.Find("args")->At(0).Find("required")->AsBool());
            // 選べる値(型の登録簿にある Actor の子孫)と、既定値
            const husky::engine::ArchiveValue* choices = command.Find("args")->At(0).Find("choices");
            REQUIRE(choices != nullptr);
            CHECK(choices->GetArraySize() >= 1);
            CHECK(choices->At(0).Find("value")->AsString() == "Actor");
            REQUIRE(command.Find("args")->At(1).Find("default") != nullptr);
            CHECK(command.Find("args")->At(1).Find("default")->AsFloat() == 0.0);
        }
    }
    CHECK(found);
}

TEST_CASE("EditorCli: コマンドが失敗したら終了コード 1。知らないコマンドと壊れた引数は 2")
{
    husky::tests::TestFolder folder("husky_test_cli_fail");
    folder.Write("textures/gone.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a72" })"); // 画像だけを消した
    std::string out;
    CHECK(RunEditorCli({"asset.refresh"}, folder.Root(), out) == CliExitCode::CommandFailed);
    CHECK(out.find("FAILED:") != std::string::npos);
    CHECK(out.find(".meta without asset: 1") != std::string::npos);

    out.clear();
    CHECK(RunEditorCli({"no.such.command"}, folder.Root(), out) == CliExitCode::UsageError);
    CHECK(out.find("--list") != std::string::npos);

    out.clear();
    CHECK(RunEditorCli({"asset.refresh", "{broken"}, folder.Root(), out) == CliExitCode::UsageError);
    out.clear();
    CHECK(RunEditorCli({"asset.refresh", "[1, 2]"}, folder.Root(), out) == CliExitCode::UsageError); // オブジェクトでない
    out.clear();
    CHECK(RunEditorCli({"asset.refresh", "{}", "extra"}, folder.Root(), out) == CliExitCode::UsageError);
}

#endif
