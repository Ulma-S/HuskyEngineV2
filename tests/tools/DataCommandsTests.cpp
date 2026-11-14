// データのアセット(ScriptableData)のエディタのコマンド data.create / data.set のテスト(SD-3a。開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

#include "core/AssetRef.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/JsonFormat.h"
#include "engine/ScriptableData.h"
#include "platform/FileSystem.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::tools::EditorCommandRegistry;
using husky::tools::EditorCommandResult;

namespace
{
    struct TintColor
    {
        float r = 1.0f;
        float g = 1.0f;
    };
    HUSKY_STRUCT_BEGIN(TintColor)
        HUSKY_PROPERTY(r)
        HUSKY_PROPERTY(g)
    HUSKY_STRUCT_END()

    class EnemySettings : public husky::engine::ScriptableData
    {
        HUSKY_OBJECT_INLINE(EnemySettings, husky::engine::ScriptableData)
    public:
        float speed = 100.0f;
        std::int32_t hp = 3;
        TintColor tint;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(speed)
            HUSKY_PROPERTY(hp)
            HUSKY_PROPERTY(tint)
        HUSKY_REFLECTION_END()
    };

    ArchiveValue Args(const char* json)
    {
        ArchiveValue args;
        REQUIRE(husky::engine::JsonFormat::Read(json, args));
        return args;
    }
}

TEST_CASE("SD-3a: data.create で型の既定の値の .sdata と .meta を作り、data.set で値を変えるとファイルが変わり、使う側も乗り換える")
{
    husky::tests::TestFolder folder("husky_test_data_commands");
    folder.Write("readme.txt", "assets"); // 空のフォルダは Scan できないことがあるので、1つ置いておく
    Application app;
    app.GetTypeRegistry().Register<EnemySettings>();
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeCreateDataCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSetDataCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));

    // --- data.create: 型の選択肢、ファイルと .meta、表に入る。上書き・外・違う拡張子・ScriptableData でない型は断る ---
    const auto typeChoices = commands.Find("data.create")->args[0].choices(app, ArchiveValue());
    REQUIRE(typeChoices.size() == 1);
    CHECK(typeChoices[0].value == "EnemySettings");

    const EditorCommandResult created =
        commands.Execute("data.create", app, Args(R"({"type": "EnemySettings", "path": "data/enemy.sdata"})"));
    REQUIRE(created.succeeded);
    CHECK(husky::platform::FileExists(folder.PathOf("data/enemy.sdata").c_str()));
    CHECK(husky::platform::FileExists(folder.PathOf("data/enemy.sdata.meta").c_str()));
    const husky::core::Guid* guid = app.GetAssetDatabase().FindGuid("data/enemy.sdata");
    REQUIRE(guid != nullptr);
    CHECK(guid->ToString() == created.data.Find("guid")->AsString());
    CHECK_FALSE(commands.Execute("data.create", app, Args(R"({"type": "EnemySettings", "path": "data/enemy.sdata"})")).succeeded);
    CHECK_FALSE(commands.Execute("data.create", app, Args(R"({"type": "EnemySettings", "path": "../enemy.sdata"})")).succeeded);
    CHECK_FALSE(commands.Execute("data.create", app, Args(R"({"type": "EnemySettings", "path": "enemy.json"})")).succeeded);
    CHECK_FALSE(commands.Execute("data.create", app, Args(R"({"type": "Actor", "path": "actor.sdata"})")).succeeded);

    // 使う側(ゲームのコードの AssetRef)。作ったファイルは既定の値。
    husky::core::AssetRef<EnemySettings> settings;
    settings.guid = *guid;
    husky::engine::AssetLoader& loader = app.GetAssetLoader();
    loader.Resolve(settings);
    loader.WaitForAllLoads();
    const EnemySettings* before = loader.Resolve(settings);
    REQUIRE(before != nullptr);
    CHECK(before->speed == 100.0f);
    CHECK(before->hp == 3);

    // --- data.set: パスでも GUID でも指せる。ファイルを書いて読み直し、AssetRef は新しい中身に乗り換える ---
    const auto propertyChoices = commands.Find("data.set")->args[1].choices(app, Args(R"({"asset": "data/enemy.sdata"})"));
    CHECK(propertyChoices.size() == 4); // speed、hp、tint.r、tint.g

    const EditorCommandResult set =
        commands.Execute("data.set", app, Args(R"({"asset": "data/enemy.sdata", "property": "speed", "value": 250})"));
    REQUIRE(set.succeeded);
    CHECK(set.message == "Set data/enemy.sdata.speed = 250");
    std::string text;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("data/enemy.sdata").c_str(), text));
    CHECK(text.find("250") != std::string::npos);
    CHECK(before->speed == 100.0f); // 読み込んだ古い中身は書き換えない(読むだけのデータ)
    const EnemySettings* after = loader.Resolve(settings);
    REQUIRE(after != nullptr);
    CHECK(after != before);
    CHECK(after->speed == 250.0f);
    CHECK(after->hp == 3); // ほかの値はそのまま
    CHECK(after->IsReadOnly());

    // 構造体の中も、GUID でも指せる。
    const std::string byGuid = R"({"asset": ")" + guid->ToString() + R"(", "property": "tint.g", "value": 0.25})";
    REQUIRE(commands.Execute("data.set", app, Args(byGuid.c_str())).succeeded);
    CHECK(loader.Resolve(settings)->tint.g == 0.25f);
    CHECK(loader.Resolve(settings)->tint.r == 1.0f);

    // --- 断る: 無いプロパティ、種類の違う値、無いアセット。断ったときはファイルを変えない ---
    CHECK_FALSE(commands.Execute("data.set", app, Args(R"({"asset": "data/enemy.sdata", "property": "nope", "value": 1})")).succeeded);
    CHECK_FALSE(commands.Execute("data.set", app, Args(R"({"asset": "data/enemy.sdata", "property": "hp", "value": "many"})")).succeeded);
    CHECK_FALSE(commands.Execute("data.set", app, Args(R"({"asset": "data/none.sdata", "property": "hp", "value": 1})")).succeeded);
    CHECK(loader.Resolve(settings)->hp == 3);

    // --- Undo: 前の値の data.set(tint.g → 1、speed → 100 の順に戻る) ---
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(loader.Resolve(settings)->tint.g == 1.0f);
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(loader.Resolve(settings)->speed == 100.0f);
}

TEST_CASE("SD-3a: 読み直した後、古い箱を指す人がいなくて新しい箱が回収されても、古い箱を指す AssetRef は読み直して乗り換える")
{
    husky::tests::TestFolder folder("husky_test_data_reload_gc");
    folder.Write("readme.txt", "assets");
    Application app;
    app.GetTypeRegistry().Register<EnemySettings>();
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeCreateDataCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSetDataCommand()));
    REQUIRE(commands.Execute("data.create", app, Args(R"({"type": "EnemySettings", "path": "enemy.sdata"})")).succeeded);

    husky::core::AssetRef<EnemySettings> settings;
    settings.guid = *app.GetAssetDatabase().FindGuid("enemy.sdata");
    husky::engine::AssetLoader& loader = app.GetAssetLoader();
    loader.Resolve(settings);
    loader.WaitForAllLoads();
    REQUIRE(loader.Resolve(settings) != nullptr);
    app.GetRegistry().AddRoot(settings.asset); // 古い箱だけを生かしておく(テストの AssetRef はプロパティではないので)

    REQUIRE(commands.Execute("data.set", app, Args(R"({"asset": "enemy.sdata", "property": "hp", "value": 9})")).succeeded);
    app.GetRegistry().CollectGarbage(); // 新しい箱は誰も指していないので回収される
    CHECK(loader.GetLoadedScriptableDataCount() == 0);

    const EnemySettings* stale = loader.Resolve(settings); // 新しい箱を読み始め、読み終わるまでは古い中身
    REQUIRE(stale != nullptr);
    CHECK(stale->hp == 3);
    loader.WaitForAllLoads();
    const EnemySettings* fresh = loader.Resolve(settings);
    REQUIRE(fresh != nullptr);
    CHECK(fresh->hp == 9);
}
#endif
