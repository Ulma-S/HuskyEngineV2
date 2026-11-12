// AssetDatabase(GUID → パスの表)のテスト。
// 本物のファイルを使う。テストの exe のフォルダの中に、テスト用のフォルダを作って並べ、終わったら消す。
#include "engine/AssetDatabase.h"

#include "platform/FileSystem.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::Guid;
using husky::engine::AssetDatabase;
using husky::engine::AssetScanReport;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;
namespace platform = husky::platform;

TEST_CASE("AssetDatabase: .meta の中身は、作ったものを読み戻せる。形が違うものは読まない")
{
    const Guid guid = MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a43ull);
    Guid loaded;
    REQUIRE(AssetDatabase::ParseMetaText(AssetDatabase::MakeMetaText(guid), loaded));
    CHECK(loaded == guid);

    CHECK_FALSE(AssetDatabase::ParseMetaText("not json", loaded));
    CHECK_FALSE(AssetDatabase::ParseMetaText("{}", loaded));                    // guid が無い
    CHECK_FALSE(AssetDatabase::ParseMetaText("{\"guid\": 12}", loaded));        // 文字列でない
    CHECK_FALSE(AssetDatabase::ParseMetaText("{\"guid\": \"abc\"}", loaded));   // GUID の形でない
    CHECK_FALSE(AssetDatabase::ParseMetaText(                                    // すべて 0(どこも指さない)
        "{\"guid\": \"00000000-0000-0000-0000-000000000000\"}", loaded));
    CHECK(loaded == guid); // 失敗したときは変えない
}

TEST_CASE("AssetDatabase: フォルダの中の .meta から表を作る。問題のあるものは表に入れずに数える")
{
    TestFolder folder("husky_test_assets_scan");
    const Guid enemy = MakeGuid(1, 1);
    const Guid player = MakeGuid(2, 2);
    const Guid copied = MakeGuid(3, 3);
    folder.WriteAsset("textures/enemy.png", enemy);
    folder.WriteAsset("textures/characters/player.png", player);
    folder.Write("sounds/jump.wav", "dummy");                      // .meta が無い
    folder.Write("textures/broken.png", "dummy");
    folder.Write("textures/broken.png.meta", "{\"guid\": 1}");     // 読めない .meta
    folder.Write("textures/deleted.png.meta", AssetDatabase::MakeMetaText(MakeGuid(4, 4))); // 対の無い .meta
    folder.WriteAsset("textures/copy_a.png", copied);              // .meta ごとコピーして GUID が重なった
    folder.WriteAsset("textures/copy_b.png", copied);
    folder.Write(".gitkeep", "");                                  // 扱わない
    folder.Write("textures/saving.png.tmp", "dummy");              // 扱わない(書きかけ)

    AssetDatabase database;
    AssetScanReport report;
    REQUIRE(database.Scan(folder.Root(), &report));

    CHECK(report.assetCount == 2);
    CHECK(report.missingMetaCount == 1);
    CHECK(report.invalidMetaCount == 1);
    CHECK(report.orphanMetaCount == 1);
    CHECK(report.duplicateGuidCount == 2); // 重なった2つとも、どちらも使わない
    CHECK(report.HasProblems());
    CHECK(database.GetAssetCount() == 2);

    // GUID → パス
    REQUIRE(database.FindPath(enemy) != nullptr);
    CHECK(*database.FindPath(enemy) == "textures/enemy.png");
    CHECK(*database.FindPath(player) == "textures/characters/player.png");
    CHECK(database.GetFullPath(enemy) == folder.Root() + "/textures/enemy.png");
    CHECK(database.FindPath(copied) == nullptr);
    CHECK(database.FindPath(MakeGuid(9, 9)) == nullptr);
    CHECK(database.GetFullPath(MakeGuid(9, 9)).empty());

    // パス → GUID(逆引き)
    REQUIRE(database.FindGuid("textures/enemy.png") != nullptr);
    CHECK(*database.FindGuid("textures/enemy.png") == enemy);
    CHECK(database.FindGuid("sounds/jump.wav") == nullptr);
}

TEST_CASE("AssetDatabase: .meta と一緒にファイルを動かしても、同じ GUID で新しい場所を引ける")
{
    TestFolder folder("husky_test_assets_move");
    const Guid enemy = MakeGuid(5, 5);
    folder.WriteAsset("enemy.png", enemy);

    AssetDatabase database;
    REQUIRE(database.Scan(folder.Root()));
    CHECK(*database.FindPath(enemy) == "enemy.png");

    // フォルダを整理して、別の場所・別の名前に動かした(.meta も一緒に)。
    folder.Remove("enemy.png");
    folder.Remove("enemy.png.meta");
    folder.WriteAsset("characters/enemies/slime.png", enemy);

    AssetScanReport report;
    REQUIRE(database.Scan(folder.Root(), &report));
    CHECK_FALSE(report.HasProblems());
    REQUIRE(database.FindPath(enemy) != nullptr);
    CHECK(*database.FindPath(enemy) == "characters/enemies/slime.png"); // GUID で指していた側は、何も直さなくてよい
}

TEST_CASE("AssetDatabase: 無いフォルダは読めない")
{
    AssetDatabase database;
    AssetScanReport report;
    CHECK_FALSE(database.Scan(platform::GetExecutableDirectory() + "husky_test_assets_missing_folder", &report));
    CHECK(database.GetAssetCount() == 0);
}

#if HUSKY_DEVELOPMENT
TEST_CASE("AssetDatabase: 【開発ビルド】.meta が無いアセットにだけ GUID を振り、写し先にも同じ .meta を書く")
{
    TestFolder source("husky_test_assets_source");
    TestFolder mirror("husky_test_assets_mirror");
    const Guid existing = MakeGuid(6, 6);
    source.WriteAsset("textures/old.png", existing);
    source.Write("textures/new.png", "dummy");
    mirror.Write("textures/new.png", "dummy");

    CHECK(husky::engine::CreateMissingMetaFiles(source.Root(), mirror.Root()) == 1); // new.png の分だけ
    CHECK(husky::engine::CreateMissingMetaFiles(source.Root(), mirror.Root()) == 0); // 2回目は何も作らない

    AssetDatabase sourceDatabase;
    AssetScanReport report;
    REQUIRE(sourceDatabase.Scan(source.Root(), &report));
    CHECK_FALSE(report.HasProblems());
    CHECK(report.assetCount == 2);
    CHECK(*sourceDatabase.FindGuid("textures/old.png") == existing); // もとの GUID は変えない
    const Guid* created = sourceDatabase.FindGuid("textures/new.png");
    REQUIRE(created != nullptr);

    // 写し先にも、同じ GUID の .meta がある。
    AssetDatabase mirrorDatabase;
    REQUIRE(mirrorDatabase.Scan(mirror.Root()));
    REQUIRE(mirrorDatabase.FindGuid("textures/new.png") != nullptr);
    CHECK(*mirrorDatabase.FindGuid("textures/new.png") == *created);
}
#endif
