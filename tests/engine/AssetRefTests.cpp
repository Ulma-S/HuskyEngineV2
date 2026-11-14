// AssetRef(アセットへの参照のプロパティ)のテスト: 保存と読み込み、使うときに読むこと、GC から守ること。
// GPU は使わない。
#include "core/AssetRef.h"

#include "engine/Application.h"
#include "engine/ObjectSerializer.h"
#include "engine/SpriteComponent.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

using husky::core::Guid;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::engine::SpriteComponent;
using husky::engine::TextureAsset;
using husky::tests::MakeGuid;
namespace ObjectSerializer = husky::engine::ObjectSerializer;

namespace
{
    // exe の隣の assets で表を作り、relativePath の GUID を返す。
    Guid ScanAndFind(Application& app, const char* relativePath)
    {
        REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
        const Guid* guid = app.GetAssetDatabase().FindGuid(relativePath);
        REQUIRE(guid != nullptr);
        return *guid;
    }
}

TEST_CASE("AssetRef: 保存すると GUID の文字列になり、読み戻すと同じ GUID を指す(ハンドルは保存しない)")
{
    Application app;
    SpriteComponent* sprite = app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    sprite->texture.guid = MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a43ull);
    sprite->texture.asset = husky::core::ObjectHandle{5, 1}; // 実行中だけの値(保存されないこと)

    const ArchiveValue saved = ObjectSerializer::Write(*sprite);
    const ArchiveValue* texture = saved.Find("properties")->Find("texture");
    REQUIRE(texture != nullptr);
    REQUIRE(texture->GetType() == ArchiveValue::Type::String);
    CHECK(texture->AsString() == "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43");

    SpriteComponent* loaded = app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    loaded->texture.asset = husky::core::ObjectHandle{9, 9};
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(saved, *loaded);
    CHECK(report.warnings.empty());
    CHECK(loaded->texture.guid == sprite->texture.guid);
    CHECK(loaded->texture.asset.IsNull()); // 前に読んでいたアセットは手放す(使うときに読み直す)
}

TEST_CASE("AssetRef: 何も指さなければ null で保存する。GUID でない文字列は読まずに警告する")
{
    Application app;
    SpriteComponent* sprite = app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    const ArchiveValue saved = ObjectSerializer::Write(*sprite);
    CHECK(saved.Find("properties")->Find("texture")->IsNull());

    sprite->texture.guid = MakeGuid(1, 1);
    ArchiveValue properties = ArchiveValue::MakeObject();
    properties.Set("texture", ArchiveValue("not a guid"));
    ArchiveValue root = ArchiveValue::MakeObject();
    root.Set("properties", std::move(properties));
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(root, *sprite);
    CHECK(report.warnings.size() == 1);
    CHECK(sprite->texture.guid == MakeGuid(1, 1)); // 読めなかったので変えない

    ObjectSerializer::ReadProperties(saved, *sprite); // null を読むと、何も指さなくなる
    CHECK(sprite->texture.IsNull());
}

TEST_CASE("AssetRef: 使うとき(Resolve)に読み、参照している間は GC に回収されない")
{
    Application app;
    const Guid guid = ScanAndFind(app, "textures/test_sprite.png");
    auto& loader = app.GetAssetLoader();

    Actor* actor = app.GetScene().Spawn<Actor>();
    SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
    sprite->texture.guid = guid;
    CHECK(loader.GetLoadedCount<husky::engine::TextureAsset>() == 0); // GUID を入れただけでは、まだ読まない

    const TextureAsset* texture = loader.Resolve(sprite->texture);
    REQUIRE(texture != nullptr);
    CHECK(texture->GetState() == husky::engine::AssetState::Loading); // 読み始めたところ
    loader.WaitForAllLoads();
    CHECK(texture->IsLoaded());
    CHECK(sprite->texture.asset == texture->GetHandle()); // 読んだアセットを覚えておく
    CHECK(loader.Resolve(sprite->texture) == texture);    // 2回目は読まずに同じものを返す

    // シーン(根)→ アクター → コンポーネント → AssetRef → アセット、とたどれるので回収されない。
    app.GetRegistry().CollectGarbage();
    CHECK(loader.Resolve<husky::engine::TextureAsset>(sprite->texture.asset) == texture);

    // アクターを消すと、アセットを指すものが無くなり、次の GC で回収される。
    actor->Destroy();
    app.GetClock().SetFixedDelta(0.01);
    app.StepFrame(0, {}); // フレームの終わりに、アクターが一覧から外れ、破棄が確定する
    app.GetRegistry().CollectGarbage();
    CHECK(loader.GetLoadedCount<husky::engine::TextureAsset>() == 0);
}

TEST_CASE("AssetRef: 指す GUID を変えると(インスペクタで選び直すなど)、次の Resolve で新しいアセットを読む")
{
    Application app;
    const Guid first = ScanAndFind(app, "textures/test_sprite.png");
    const Guid* second = app.GetAssetDatabase().FindGuid("textures/checker.png");
    REQUIRE(second != nullptr);
    auto& loader = app.GetAssetLoader();

    SpriteComponent* sprite = app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    sprite->texture.guid = first;
    REQUIRE(loader.Resolve(sprite->texture) != nullptr);

    sprite->texture.guid = *second; // asset には古いアセットのハンドルが残っているが、GUID が違うので使わない
    const TextureAsset* texture = loader.Resolve(sprite->texture);
    REQUIRE(texture != nullptr);
    CHECK(texture->GetGuid() == *second);

    sprite->texture.guid = {}; // 何も指さない
    CHECK(loader.Resolve(sprite->texture) == nullptr);
    CHECK(sprite->texture.asset.IsNull());
}
