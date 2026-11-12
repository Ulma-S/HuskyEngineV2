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
