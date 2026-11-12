// SpriteSlices(.meta の切り出し方)のテスト: 読む、ID を振る、テクスチャと一緒に読み込む。
#include "engine/SpriteSlices.h"

#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>
#include <unordered_set>
#include <vector>

using husky::engine::ArchiveValue;
using husky::engine::ReadSpriteSlices;
using husky::engine::SpriteSlice;
namespace JsonFormat = husky::engine::JsonFormat;

namespace
{
    ArchiveValue ParseJson(const char* text)
    {
        ArchiveValue value;
        REQUIRE(JsonFormat::Read(text, value));
        return value;
    }
}

TEST_CASE("SpriteSlices: .meta の sprites から、ID、名前、範囲を読む。sprites が無ければ空")
{
    const ArchiveValue meta = ParseJson(R"({
        "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43",
        "sprites": [
            { "id": 11, "name": "circle",  "x": 0,  "y": 0,  "width": 32, "height": 32 },
            { "id": 22, "name": "diamond", "x": 32, "y": 32, "width": 16, "height": 8 }
        ]
    })");
    std::vector<std::string> warnings;
    const std::vector<SpriteSlice> slices = ReadSpriteSlices(meta, &warnings);
    CHECK(warnings.empty());
    REQUIRE(slices.size() == 2);
    CHECK(slices[1].id == 22);
    CHECK(slices[1].name == "diamond");
    CHECK(slices[1].rect.x == 32);
    CHECK(slices[1].rect.width == 16);
    CHECK(slices[1].rect.height == 8);

    CHECK(ReadSpriteSlices(ParseJson(R"({ "guid": "x" })")).empty());
}

TEST_CASE("SpriteSlices: 形がおかしい切り出しは飛ばし、理由を残す")
{
    const ArchiveValue meta = ParseJson(R"({ "sprites": [
        { "id": 1, "name": "ok",        "x": 0, "y": 0, "width": 8, "height": 8 },
        { "id": 2, "name": "no width",  "x": 0, "y": 0,             "height": 8 },
        { "id": 3, "name": "zero size", "x": 0, "y": 0, "width": 0, "height": 8 },
        {          "name": "no id",     "x": 0, "y": 0, "width": 8, "height": 8 },
        { "id": 1, "name": "same id",   "x": 8, "y": 0, "width": 8, "height": 8 }
    ] })");
    std::vector<std::string> warnings;
    const std::vector<SpriteSlice> slices = ReadSpriteSlices(meta, &warnings);
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].name == "ok");
    CHECK(warnings.size() == 4);
}

#if HUSKY_DEVELOPMENT
TEST_CASE("SpriteSlices: 【開発ビルド】ID が無い・0・重なる切り出しにだけ ID を振る。ある ID は変えない")
{
    using husky::engine::AssignMissingSliceIds;

    ArchiveValue meta = ParseJson(R"({ "guid": "x", "sprites": [
        { "id": 5, "name": "keep",    "x": 0, "y": 0, "width": 8, "height": 8 },
        {          "name": "missing", "x": 8, "y": 0, "width": 8, "height": 8 },
        { "id": 0, "name": "zero",    "x": 0, "y": 8, "width": 8, "height": 8 },
        { "id": 5, "name": "same",    "x": 8, "y": 8, "width": 8, "height": 8 }
    ] })");
    CHECK(AssignMissingSliceIds(meta) == 3);
    CHECK(AssignMissingSliceIds(meta) == 0); // 2回目は振るものが無い

    std::vector<std::string> warnings;
    const std::vector<SpriteSlice> slices = ReadSpriteSlices(meta, &warnings);
    CHECK(warnings.empty());
    REQUIRE(slices.size() == 4);
    CHECK(slices[0].id == 5);          // もとの ID はそのまま
    CHECK(slices[1].name == "missing"); // 順番も、ほかのメンバーもそのまま
    std::unordered_set<std::int32_t> ids;
    for (const SpriteSlice& slice : slices)
    {
        CHECK(slice.id != 0);
        CHECK(ids.insert(slice.id).second); // 重ならない
    }
    // 振った ID は、要素の先頭に書く(JSON を見たときに分かりやすいように)。
    CHECK(meta.Find("sprites")->At(1).GetMemberName(0) == "id");
}

TEST_CASE("SpriteSlices: 【開発ビルド】フォルダの .meta に ID を振って書き戻し、写し先にも同じものを書く")
{
    husky::tests::TestFolder source("husky_test_slices_source");
    husky::tests::TestFolder mirror("husky_test_slices_mirror");
    const char* metaText = R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43",
        "sprites": [ { "name": "a", "x": 0, "y": 0, "width": 8, "height": 8 } ] })";
    source.Write("textures/sheet.png.meta", metaText);
    source.Write("textures/plain.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a44" })");
    mirror.Write("textures/sheet.png.meta", metaText);

    CHECK(husky::engine::AssignMissingSliceIdsInFolder(source.Root(), mirror.Root()) == 1); // plain は切り出しが無い
    CHECK(husky::engine::AssignMissingSliceIdsInFolder(source.Root(), mirror.Root()) == 0);

    std::string sourceText;
    std::string mirrorText;
    REQUIRE(husky::platform::ReadTextFile(source.PathOf("textures/sheet.png.meta").c_str(), sourceText));
    REQUIRE(husky::platform::ReadTextFile(mirror.PathOf("textures/sheet.png.meta").c_str(), mirrorText));
    CHECK(sourceText == mirrorText);
    ArchiveValue written;
    REQUIRE(JsonFormat::Read(sourceText, written));
    CHECK(ReadSpriteSlices(written).size() == 1);
}
#endif

TEST_CASE("SpriteSlices: テクスチャと一緒に読み込まれる。画像からはみ出す切り出しは使わない")
{
    // 本物の PNG(32x32)を、exe の隣の assets から借りて、テスト用のフォルダに置く。
    std::string png;
    REQUIRE(husky::platform::ReadTextFile(
        (husky::platform::GetExecutableDirectory() + "assets/textures/test_sprite.png").c_str(), png));
    husky::tests::TestFolder folder("husky_test_slices_loader");
    folder.Write("sheet.png", png);
    folder.Write("sheet.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a45", "sprites": [
        { "id": 5, "name": "inside",  "x": 16, "y": 16, "width": 16, "height": 16 },
        { "id": 6, "name": "outside", "x": 24, "y": 0,  "width": 16, "height": 16 }
    ] })");

    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const husky::core::Guid* guid = app.GetAssetDatabase().FindGuid("sheet.png");
    REQUIRE(guid != nullptr);
    const husky::core::ObjectHandle handle = app.GetAssetLoader().LoadTexture(*guid);
    app.GetAssetLoader().WaitForAllLoads();

    const husky::engine::TextureAsset* texture = app.GetAssetLoader().ResolveTexture(handle);
    REQUIRE(texture != nullptr);
    REQUIRE(texture->IsLoaded());
    REQUIRE(texture->GetSlices().size() == 1);
    REQUIRE(texture->FindSlice(5) != nullptr);
    CHECK(texture->FindSlice(5)->name == "inside");
    CHECK(texture->FindSlice(6) == nullptr); // 32 の幅からはみ出す(24 + 16 = 40)
    CHECK(texture->FindSlice(0) == nullptr);
}
