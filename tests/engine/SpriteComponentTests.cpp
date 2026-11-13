// SpriteComponent と ForEachSprite のテスト。GPU は使わない(何を描くかだけを確かめる)。
#include "engine/SpriteComponent.h"

#include "engine/ObjectSerializer.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <vector>

using husky::core::ObjectRegistry;
using husky::engine::Actor;
using husky::engine::ForEachSprite;
using husky::engine::Scene;
using husky::engine::SpriteComponent;
using husky::render::Sprite;

namespace
{
    Scene* CreateRootScene(ObjectRegistry& registry)
    {
        Scene* scene = registry.Create<Scene>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    // ForEachSprite が渡すスプライトを、順に集める。
    std::vector<Sprite> CollectSprites(Scene& scene)
    {
        std::vector<Sprite> sprites;
        ForEachSprite(scene, [&](const Sprite& sprite, SpriteComponent&) { sprites.push_back(sprite); });
        return sprites;
    }
}

TEST_CASE("SpriteComponent: 既定は 32x32 の白(テクスチャのまま)")
{
    const SpriteComponent sprite;
    CHECK(sprite.width == 32.0f);
    CHECK(sprite.height == 32.0f);
    CHECK(sprite.color.r == 1.0f);
    CHECK(sprite.color.g == 1.0f);
    CHECK(sprite.color.b == 1.0f);
    CHECK(sprite.color.a == 1.0f);
}

TEST_CASE("SpriteComponent: 大きさはリフレクションのプロパティとして見える")
{
    CHECK(SpriteComponent::StaticType().FindProperty("width") != nullptr);
    CHECK(SpriteComponent::StaticType().FindProperty("height") != nullptr);
}

TEST_CASE("ForEachSprite: 位置はアクターから、大きさと色はコンポーネントから取り、一覧の順に渡す")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);

    Actor* first = scene->Spawn<Actor>();
    first->transform.position.x = 10.0f;
    first->transform.position.y = 20.0f;
    SpriteComponent* firstSprite = first->AddComponent<SpriteComponent>();
    firstSprite->width = 64.0f;
    firstSprite->height = 48.0f;
    firstSprite->color = {0.5f, 0.25f, 1.0f, 1.0f};

    scene->Spawn<Actor>(); // スプライトを持たないアクターは描かない

    Actor* third = scene->Spawn<Actor>();
    third->transform.position.x = 100.0f;
    third->AddComponent<SpriteComponent>();

    const std::vector<Sprite> sprites = CollectSprites(*scene);

    REQUIRE(sprites.size() == 2);
    CHECK(sprites[0].model[3][0] == 10.0f);
    CHECK(sprites[0].model[3][1] == 20.0f);
    CHECK(sprites[0].width == 64.0f);
    CHECK(sprites[0].height == 48.0f);
    CHECK(sprites[0].color.g == 0.25f);
    CHECK(sprites[1].model[3][0] == 100.0f); // 一覧の順(奥から手前)。model[3] は行列の4列目 = 移動の分
    CHECK(sprites[1].width == 32.0f);
}

TEST_CASE("ForEachSprite: Destroy したアクターやスプライトは描かない")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);

    Actor* destroyedActor = scene->Spawn<Actor>();
    destroyedActor->AddComponent<SpriteComponent>();
    Actor* withDestroyedSprite = scene->Spawn<Actor>();
    withDestroyedSprite->AddComponent<SpriteComponent>()->Destroy();
    Actor* alive = scene->Spawn<Actor>();
    alive->transform.position.x = 5.0f;
    alive->AddComponent<SpriteComponent>();

    destroyedActor->Destroy();

    const std::vector<Sprite> sprites = CollectSprites(*scene);

    REQUIRE(sprites.size() == 1);
    CHECK(sprites[0].model[3][0] == 5.0f);
}

TEST_CASE("SpriteComponent: テクスチャは、テクスチャのアセットへの参照(AssetRef)のプロパティ")
{
    const husky::core::Property* texture = SpriteComponent::StaticType().FindProperty("texture");
    REQUIRE(texture != nullptr);
    CHECK(texture->GetType() == husky::core::PropertyType::AssetRef);
    CHECK(texture->GetAssetType() == &husky::engine::TextureAsset::StaticType()); // インスペクタはこれで絞り込む
    CHECK(texture->GetStructType() == nullptr);
    CHECK_FALSE(texture->IsReference()); // シーンの中のオブジェクトへの参照ではない(保存に番号の対応表は要らない)
}

TEST_CASE("ForEachSprite: コンポーネントも一緒に渡す(描画層のテクスチャは、DrawSprites がアセットから引く)")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    SpriteComponent* sprite = scene->Spawn<Actor>()->AddComponent<SpriteComponent>();
    sprite->texture.guid = husky::tests::MakeGuid(3, 2);

    int count = 0;
    ForEachSprite(*scene, [&](const Sprite& drawn, SpriteComponent& component) {
        CHECK(drawn.texture.IsNull());
        CHECK(&component == sprite);
        ++count;
    });
    CHECK(count == 1);
}

TEST_CASE("ApplyRegion: ピクセルの範囲を、画像の大きさで割って UV(0〜1)にする")
{
    Sprite sprite;
    husky::engine::ApplyRegion({32, 0, 32, 16}, 64, 64, sprite); // 64x64 の右上の 32x16
    CHECK(sprite.u0 == 0.5f);
    CHECK(sprite.v0 == 0.0f);
    CHECK(sprite.u1 == 1.0f);
    CHECK(sprite.v1 == 0.25f);
}

TEST_CASE("ApplyRegion: 範囲が空、または画像の大きさが分からなければ、画像の全体")
{
    Sprite sprite;
    sprite.u0 = 0.5f; // 前の値が残っていても、全体に戻す
    husky::engine::ApplyRegion({}, 64, 64, sprite);
    CHECK(sprite.u0 == 0.0f);
    CHECK(sprite.v0 == 0.0f);
    CHECK(sprite.u1 == 1.0f);
    CHECK(sprite.v1 == 1.0f);

    husky::engine::ApplyRegion({0, 0, 16, 16}, 0, 0, sprite);
    CHECK(sprite.u1 == 1.0f);
}

TEST_CASE("SpriteComponent: 範囲(region)は構造体のプロパティで、保存して読み戻せる")
{
    using husky::engine::ArchiveValue;
    namespace ObjectSerializer = husky::engine::ObjectSerializer;

    ObjectRegistry registry;
    auto* sprite = registry.Create<SpriteComponent>();
    sprite->region = {32, 32, 16, 8};
    const ArchiveValue saved = ObjectSerializer::Write(*sprite);
    const ArchiveValue* region = saved.Find("properties")->Find("region");
    REQUIRE(region != nullptr);
    CHECK(region->Find("width")->AsInt() == 16);

    auto* copy = registry.Create<SpriteComponent>();
    CHECK(ObjectSerializer::ReadProperties(saved, *copy).warnings.empty());
    CHECK(copy->region.x == 32);
    CHECK(copy->region.y == 32);
    CHECK(copy->region.width == 16);
    CHECK(copy->region.height == 8);
}

namespace
{
    // スプライトを1枚だけ持つシーンから、描く四隅を計算する(ForEachSprite → ComputeSpriteCorners)。
    husky::render::SpriteCorners CornersOf(Scene& scene)
    {
        const std::vector<Sprite> sprites = CollectSprites(scene);
        REQUIRE(sprites.size() == 1);
        return husky::render::ComputeSpriteCorners(sprites[0]);
    }
}

TEST_CASE("RS-1: ピボットは既定で真ん中。位置がスプライトの真ん中になり、四隅は大きさの半分ずつ離れる")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    actor->transform.position = {100.0f, 50.0f, 0.0f};
    SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
    sprite->width = 40.0f;
    sprite->height = 20.0f;
    CHECK(sprite->pivot.x == 0.5f);
    CHECK(sprite->pivot.y == 0.5f);

    const husky::render::SpriteCorners corners = CornersOf(*scene);
    // 0: 左上、1: 右上、2: 右下、3: 左下
    CHECK(corners.x[0] == doctest::Approx(80.0f));
    CHECK(corners.y[0] == doctest::Approx(60.0f));
    CHECK(corners.x[2] == doctest::Approx(120.0f));
    CHECK(corners.y[2] == doctest::Approx(40.0f));

    // ピボットを左下にすると、位置が左下の角になる(RS-1 より前の描き方)
    sprite->pivot = {0.0f, 0.0f};
    const husky::render::SpriteCorners corner = CornersOf(*scene);
    CHECK(corner.x[3] == doctest::Approx(100.0f));
    CHECK(corner.y[3] == doctest::Approx(50.0f));
    CHECK(corner.x[1] == doctest::Approx(140.0f));
    CHECK(corner.y[1] == doctest::Approx(70.0f));
}

TEST_CASE("RS-1: 回転はピボットを中心に反時計回り、拡大縮小はピボットを中心に大きくなる")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    actor->transform.position = {100.0f, 50.0f, 0.0f};
    SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
    sprite->width = 40.0f;
    sprite->height = 20.0f;

    // 90 度回すと、右上の角(ローカル (20, 10))は (-10, 20) へ動く。真ん中は動かない
    actor->transform.rotation = 90.0f;
    husky::render::SpriteCorners corners = CornersOf(*scene);
    CHECK(corners.x[1] == doctest::Approx(90.0f));
    CHECK(corners.y[1] == doctest::Approx(70.0f));
    CHECK((corners.x[0] + corners.x[2]) / 2.0f == doctest::Approx(100.0f));
    CHECK((corners.y[0] + corners.y[2]) / 2.0f == doctest::Approx(50.0f));

    // 回さずに x を 2 倍、y を 3 倍にすると、真ん中から x は 40、y は 30 離れる
    actor->transform.rotation = 0.0f;
    actor->transform.scale = {2.0f, 3.0f, 1.0f};
    corners = CornersOf(*scene);
    CHECK(corners.x[1] == doctest::Approx(140.0f));
    CHECK(corners.y[1] == doctest::Approx(80.0f));
    CHECK(corners.x[3] == doctest::Approx(60.0f));
    CHECK(corners.y[3] == doctest::Approx(20.0f));
}
