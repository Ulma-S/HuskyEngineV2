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
    CHECK(sprites[0].x == 10.0f);
    CHECK(sprites[0].y == 20.0f);
    CHECK(sprites[0].width == 64.0f);
    CHECK(sprites[0].height == 48.0f);
    CHECK(sprites[0].color.g == 0.25f);
    CHECK(sprites[1].x == 100.0f); // 一覧の順(奥から手前)
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
    CHECK(sprites[0].x == 5.0f);
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
