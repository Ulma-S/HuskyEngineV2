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

TEST_CASE("SpriteSlices: スプライトシートの sprites から、ID、名前、範囲を読む。sprites が無ければ空")
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

TEST_CASE("SpriteSlices: 【開発ビルド】フォルダの .spritesheet に ID を振って書き戻し、写し先にも同じものを書く")
{
    husky::tests::TestFolder source("husky_test_slices_source");
    husky::tests::TestFolder mirror("husky_test_slices_mirror");
    const char* sheetText = R"({ "texture": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43",
        "sprites": [ { "name": "a", "x": 0, "y": 0, "width": 8, "height": 8 } ] })";
    source.Write("sprites/sheet.spritesheet", sheetText);
    source.Write("sprites/plain.spritesheet", R"({ "texture": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43" })");
    source.Write("sprites/sheet.spritesheet.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a44" })");
    mirror.Write("sprites/sheet.spritesheet", sheetText);

    CHECK(husky::engine::AssignMissingSliceIdsInFolder(source.Root(), mirror.Root()) == 1); // plain と .meta は対象外
    CHECK(husky::engine::AssignMissingSliceIdsInFolder(source.Root(), mirror.Root()) == 0);

    std::string sourceText;
    std::string mirrorText;
    REQUIRE(husky::platform::ReadTextFile(source.PathOf("sprites/sheet.spritesheet").c_str(), sourceText));
    REQUIRE(husky::platform::ReadTextFile(mirror.PathOf("sprites/sheet.spritesheet").c_str(), mirrorText));
    CHECK(sourceText == mirrorText);
    ArchiveValue written;
    REQUIRE(JsonFormat::Read(sourceText, written));
    CHECK(ReadSpriteSlices(written).size() == 1);
}
#endif

namespace
{
    // テスト用のフォルダに、本物の PNG(32x32。exe の隣の assets から借りる)と、それを指すスプライトシートを置く。
    //   sheet.png(GUID ...45)  ←  sheet.spritesheet(GUID ...46)
    void WriteSheetAssets(const husky::tests::TestFolder& folder)
    {
        std::string png;
        REQUIRE(husky::platform::ReadTextFile(
            (husky::platform::GetExecutableDirectory() + "assets/textures/test_sprite.png").c_str(), png));
        folder.Write("sheet.png", png);
        folder.Write("sheet.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a45" })");
        folder.Write("sheet.spritesheet", R"({ "texture": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a45", "sprites": [
            { "id": 5, "name": "left",  "x": 0,  "y": 0, "width": 16, "height": 32 },
            { "id": 6, "name": "right", "x": 16, "y": 0, "width": 16, "height": 32 }
        ] })");
        folder.Write("sheet.spritesheet.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a46" })");
    }
}

TEST_CASE("SpriteSheet: 読むと切り出しが分かり、中で指している画像も読み込まれる(依存の読み込み)")
{
    husky::tests::TestFolder folder("husky_test_sheet_load");
    WriteSheetAssets(folder);

    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    auto& loader = app.GetAssetLoader();
    const husky::core::ObjectHandle handle = loader.LoadSpriteSheet(husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a46ull));

    husky::engine::SpriteSheetAsset* sheet = loader.ResolveSpriteSheet(handle);
    REQUIRE(sheet != nullptr);
    CHECK(sheet->GetState() == husky::engine::AssetState::Loading);

    loader.WaitForAllLoads(); // スプライトシート → 中の画像、と続けて読み終わるまで待つ
    REQUIRE(sheet->IsLoaded());
    REQUIRE(sheet->GetSlices().size() == 2);
    REQUIRE(sheet->FindSlice(6) != nullptr);
    CHECK(sheet->FindSlice(6)->name == "right");
    CHECK(sheet->FindSlice(6)->rect.x == 16);
    CHECK(sheet->FindSlice(7) == nullptr);

    const husky::engine::TextureAsset* texture = loader.ResolveTexture(sheet->texture.asset);
    REQUIRE(texture != nullptr);
    CHECK(texture->IsLoaded());
    CHECK(texture->GetWidth() == 32);
    CHECK(loader.GetPendingLoadCount() == 0);
}

TEST_CASE("SpriteSheet: スプライトシートが生きている間は、中の画像も GC に回収されない。同じ画像を直接読むと同じテクスチャ")
{
    husky::tests::TestFolder folder("husky_test_sheet_gc");
    WriteSheetAssets(folder);

    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    auto& loader = app.GetAssetLoader();
    const husky::core::ObjectHandle sheetHandle =
        loader.LoadSpriteSheet(husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a46ull));
    app.GetRegistry().AddRoot(sheetHandle); // スプライトシートだけを根に加える
    loader.WaitForAllLoads();

    // 画像は、スプライトシートの texture(プロパティの AssetRef)からたどれるので、回収されない。
    app.GetRegistry().CollectGarbage();
    const husky::core::ObjectHandle textureHandle = loader.ResolveSpriteSheet(sheetHandle)->texture.asset;
    REQUIRE(loader.ResolveTexture(textureHandle) != nullptr);

    // 同じ画像をテクスチャとして直接読むと、スプライトシートが使っているものと同じアセットを返す(画像は1回だけ読む)。
    CHECK(loader.LoadTexture(husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a45ull)) == textureHandle);
    CHECK(loader.GetLoadedTextureCount() == 1);

    // スプライトシートを根から外すと、どちらも回収される。
    app.GetRegistry().RemoveRoot(sheetHandle);
    app.GetRegistry().CollectGarbage();
    CHECK(loader.GetLoadedSpriteSheetCount() == 0);
    CHECK(loader.GetLoadedTextureCount() == 0);
}

TEST_CASE("SpriteSheet: texture が無い・GUID でないファイルは「失敗」になる")
{
    husky::tests::TestFolder folder("husky_test_sheet_broken");
    folder.Write("broken.spritesheet", R"({ "sprites": [] })");
    folder.Write("broken.spritesheet.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a47" })");

    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const husky::core::ObjectHandle handle =
        app.GetAssetLoader().LoadSpriteSheet(husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a47ull));
    app.GetAssetLoader().WaitForAllLoads();
    CHECK(app.GetAssetLoader().ResolveSpriteSheet(handle)->GetState() == husky::engine::AssetState::Failed);
}

TEST_CASE("AssetLoader: 読めるファイルは、アセットの型ごとに拡張子で決まる")
{
    using husky::engine::AssetLoader;
    CHECK(AssetLoader::CanLoadAs(husky::engine::TextureAsset::StaticType(), "textures/a.png"));
    CHECK_FALSE(AssetLoader::CanLoadAs(husky::engine::TextureAsset::StaticType(), "sprites/a.spritesheet"));
    CHECK(AssetLoader::CanLoadAs(husky::engine::SpriteSheetAsset::StaticType(), "sprites/a.spritesheet"));
    CHECK_FALSE(AssetLoader::CanLoadAs(husky::engine::SpriteSheetAsset::StaticType(), "textures/a.png"));
}
