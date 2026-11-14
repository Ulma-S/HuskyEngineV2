// AssetLoader(アセットのローダ)と TextureAsset のテスト。GPU は使わない(ファイルを読むところまでを確かめる)。
// Application を使う(アセットの表、ローダ、レジストリを、正しい順番で持っているため)。
#include "engine/AssetLoader.h"

#include "engine/Application.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::Guid;
using husky::core::ObjectHandle;
using husky::engine::Application;
using husky::engine::AssetState;
using husky::engine::TextureAsset;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;

namespace
{
    // PNG として正しい、最小の画像(1x1 の白)のバイト列は長いので、ここでは本物の画像を、
    // ビルドのたびに exe の隣へコピーされる assets から借りる。
    const char* const TestSpritePath = "textures/test_sprite.png"; // 32x32

    // exe の隣の assets で表を作り、test_sprite.png の GUID を返す。
    Guid ScanBuiltAssets(Application& app)
    {
        REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
        const Guid* guid = app.GetAssetDatabase().FindGuid(TestSpritePath);
        REQUIRE(guid != nullptr);
        return *guid;
    }
}

TEST_CASE("AssetLoader: LoadTexture はすぐに戻り「読み込み中」。読み終わると「読めた」になり、大きさが分かる")
{
    Application app; // ワーカー 0 本(同期実行): ジョブは Update か WaitForAllLoads で実行される
    const Guid guid = ScanBuiltAssets(app);

    const ObjectHandle handle = app.GetAssetLoader().LoadTexture(guid);
    const TextureAsset* texture = app.GetAssetLoader().ResolveTexture(handle);
    REQUIRE(texture != nullptr);
    CHECK(texture->GetState() == AssetState::Loading);
    CHECK(texture->GetWidth() == 0);
    CHECK(app.GetAssetLoader().GetPendingLoadCount() == 1);

    app.GetAssetLoader().WaitForAllLoads();
    CHECK(texture->GetState() == AssetState::Loaded);
    CHECK(texture->GetGuid() == guid);
    CHECK(texture->GetWidth() == 32);
    CHECK(texture->GetHeight() == 32);
    CHECK(texture->GetTexture().IsNull()); // GPU が無いので、描画層のテクスチャはまだ無い
    CHECK(app.GetAssetLoader().GetPendingLoadCount() == 0);
}

TEST_CASE("AssetLoader: 毎フレームの StepFrame(の中の Update)で、読み終わったものが「読めた」になる")
{
    Application app;
    const Guid guid = ScanBuiltAssets(app);
    const ObjectHandle handle = app.GetAssetLoader().LoadTexture(guid);
    app.GetRegistry().AddRoot(handle); // フレームの終わりの GC で回収されないように

    app.GetClock().SetFixedDelta(0.01);
    app.StepFrame(0, {});
    CHECK(app.GetAssetLoader().ResolveTexture(handle)->IsLoaded());
}

TEST_CASE("AssetLoader: 同じ GUID を2回読むと、同じアセットを返す(読み込み中でも。2回読まない)")
{
    Application app;
    const Guid guid = ScanBuiltAssets(app);
    const ObjectHandle first = app.GetAssetLoader().LoadTexture(guid);
    const ObjectHandle second = app.GetAssetLoader().LoadTexture(guid);
    CHECK(first == second);
    CHECK(app.GetAssetLoader().GetLoadedTextureCount() == 1);
    CHECK(app.GetAssetLoader().GetPendingLoadCount() == 1);
}

TEST_CASE("AssetLoader: 読めないときも、ハンドルは返す。状態が「失敗」になる")
{
    TestFolder folder("husky_test_loader_failed");
    folder.WriteAsset("textures/broken.png", MakeGuid(7, 7)); // 中身が PNG ではない("dummy")

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));

    // 表に無い GUID: 読むファイルが分からないので、ジョブを積まずに、その場で「失敗」
    const ObjectHandle unknown = app.GetAssetLoader().LoadTexture(MakeGuid(1, 2));
    REQUIRE_FALSE(unknown.IsNull());
    CHECK(app.GetAssetLoader().ResolveTexture(unknown)->GetState() == AssetState::Failed);

    // ファイルが壊れている: 読んでみて分かるので、読み終わってから「失敗」
    const ObjectHandle broken = app.GetAssetLoader().LoadTexture(MakeGuid(7, 7));
    REQUIRE_FALSE(broken.IsNull());
    const TextureAsset* texture = app.GetAssetLoader().ResolveTexture(broken);
    CHECK(texture->GetState() == AssetState::Loading);
    app.GetAssetLoader().WaitForAllLoads();
    CHECK(texture->GetState() == AssetState::Failed);
    CHECK_FALSE(texture->IsLoaded());
    CHECK(texture->GetWidth() == 0);
}

TEST_CASE("AssetLoader: どこからも強く指されていないアセットは GC で回収され、次に読むと新しく作る")
{
    Application app;
    const Guid guid = ScanBuiltAssets(app);
    auto& loader = app.GetAssetLoader();

    const ObjectHandle first = loader.LoadTexture(guid);
    CHECK(app.GetRegistry().CollectGarbage() >= 1); // 根にもプロパティにも指されていない → 読み込み中でも回収
    CHECK(loader.ResolveTexture(first) == nullptr);
    CHECK(loader.GetLoadedTextureCount() == 0);   // キャッシュからも外れる(TextureAsset のデストラクタが外す)

    // 読み込みのジョブは残っている。終わっても、書く先が無いので結果を捨てるだけ(落ちない)。
    CHECK(loader.GetPendingLoadCount() == 1);
    loader.WaitForAllLoads();
    CHECK(loader.GetPendingLoadCount() == 0);

    const ObjectHandle second = loader.LoadTexture(guid);
    CHECK(second != first); // 新しく作った(古いハンドルは世代が違うので、取り違えない)
    REQUIRE(loader.ResolveTexture(second) != nullptr);

    // 根に加えれば、GC でも回収されない。
    app.GetRegistry().AddRoot(second);
    app.GetRegistry().CollectGarbage();
    loader.WaitForAllLoads();
    CHECK(loader.ResolveTexture(second)->IsLoaded());
    CHECK(loader.LoadTexture(guid) == second);
}

TEST_CASE("AssetLoader(ワーカーあり): 読み込みはワーカーで行われ、すべて「読めた」になる")
{
    Application app(husky::core::ObjectRegistry::DefaultCapacityBytes, 3); // ワーカー 3 本
    const Guid sprite = ScanBuiltAssets(app);
    const Guid* checker = app.GetAssetDatabase().FindGuid("textures/checker.png");
    REQUIRE(checker != nullptr);

    for (int round = 0; round < 20; ++round) // 読み込みと回収を何度も繰り返す(取り合いで壊れないこと)
    {
        const ObjectHandle first = app.GetAssetLoader().LoadTexture(sprite);
        const ObjectHandle second = app.GetAssetLoader().LoadTexture(*checker);
        app.GetAssetLoader().WaitForAllLoads();
        REQUIRE(app.GetAssetLoader().ResolveTexture(first)->IsLoaded());
        REQUIRE(app.GetAssetLoader().ResolveTexture(second)->IsLoaded());
        CHECK(app.GetAssetLoader().ResolveTexture(second)->GetWidth() == 32);
        app.GetRegistry().CollectGarbage(); // どちらも根に無いので回収される
        CHECK(app.GetAssetLoader().GetLoadedTextureCount() == 0);
    }
}

TEST_CASE("AssetLoader(ワーカーあり): 読み込み中に Application を壊しても、ジョブの終わりを待ってから壊れる")
{
    Application app(husky::core::ObjectRegistry::DefaultCapacityBytes, 2);
    const Guid guid = ScanBuiltAssets(app);
    for (int i = 0; i < 10; ++i)
    {
        app.GetAssetLoader().LoadTexture(guid); // 1つ目以外は同じアセットを返す
        app.GetRegistry().CollectGarbage();      // 回収して、次は新しく読ませる
    }
    // ここで app が壊れる。ローダはジョブを待ち、ジョブシステムはワーカーを止める(ASan のビルドで、誤用が無いことも確かめる)。
}

TEST_CASE("AssetLoader: Application はテクスチャ・音・フォント・スプライトシートの読み方を登録する(AL-2)")
{
    using husky::engine::AssetKind;
    Application app;
    husky::engine::AssetLoader& loader = app.GetAssetLoader();

    const AssetKind* texture = loader.FindKind(TextureAsset::StaticType());
    const AssetKind* sound = loader.FindKind(husky::engine::SoundAsset::StaticType());
    const AssetKind* font = loader.FindKind(husky::engine::FontAsset::StaticType());
    const AssetKind* sheet = loader.FindKind(husky::engine::SpriteSheetAsset::StaticType());
    REQUIRE(texture != nullptr);
    REQUIRE(sound != nullptr);
    REQUIRE(font != nullptr);
    REQUIRE(sheet != nullptr);
    // 種類ごとに別の読み方で、拡張子で見分ける。
    CHECK(texture->CanLoad("a.png"));
    CHECK_FALSE(texture->CanLoad("a.wav"));
    CHECK(font->CanLoad("a.ttf"));
    CHECK(font->CanLoad("a.otf"));
    CHECK(sheet->CanLoad("a.spritesheet"));
    CHECK_FALSE(sheet->CanLoad("a.png"));
    // 何も読んでいなければ、どのキャッシュも空。
    CHECK(loader.GetLoadedCount(TextureAsset::StaticType()) == 0);
    CHECK(loader.GetLoadedCount(husky::engine::SpriteSheetAsset::StaticType()) == 0);
}
