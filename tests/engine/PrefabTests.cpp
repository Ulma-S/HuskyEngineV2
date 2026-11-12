// プレハブ(engine/Prefab.h)のテスト。アクターから作り、JSON を通して、何度でも置けることを確かめる。
#include "core/ObjectRegistry.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;
using husky::core::TypeRegistry;
using husky::engine::Actor;
using husky::engine::ArchiveValue;
using husky::engine::Scene;
using husky::engine::SpriteComponent;
namespace JsonFormat = husky::engine::JsonFormat;
namespace Prefab = husky::engine::Prefab;

namespace
{
    // ほかのアクターを指す(ハンドルのプロパティを持つ)アクター。
    class Follower : public Actor
    {
        HUSKY_OBJECT(Follower, Actor)
    public:
        ObjectHandle target;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(target)
        HUSKY_REFLECTION_END()
    };

    Scene* CreateRootScene(ObjectRegistry& registry)
    {
        Scene* scene = registry.Create<Scene>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    TypeRegistry MakeTypes()
    {
        TypeRegistry types;
        husky::engine::RegisterEngineTypes(types);
        types.Register<Follower>();
        return types;
    }

    ArchiveValue ThroughJson(const ArchiveValue& value)
    {
        std::string text;
        REQUIRE(JsonFormat::Write(value, text));
        ArchiveValue loaded;
        REQUIRE(JsonFormat::Read(text, loaded));
        return loaded;
    }
}

TEST_CASE("Prefab: アクターから作ったプレハブを2回置くと、コンポーネントと値の付いた別々のアクターが2体できる")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);

    Actor* original = scene->Spawn<Actor>();
    original->name = "Coin";
    original->transform.position.x = 3.0f;
    SpriteComponent* sprite = original->AddComponent<SpriteComponent>();
    sprite->width = 24.0f;
    sprite->color = {1.0f, 0.8f, 0.0f, 1.0f};

    const Prefab::WriteResult saved = Prefab::Write(*original);
    REQUIRE(saved.ok);
    CHECK(saved.warnings.empty());
    const ArchiveValue file = ThroughJson(saved.value);

    const Prefab::InstantiateResult first = Prefab::Instantiate(file, *scene, types);
    const Prefab::InstantiateResult second = Prefab::Instantiate(file, *scene, types);
    CHECK(first.warnings.empty());
    CHECK(second.warnings.empty()); // guid を書いていないので、2回置いても「guid が重なった」は出ない
    REQUIRE(first.actor != nullptr);
    REQUIRE(second.actor != nullptr);
    CHECK(scene->actors.size() == 3); // 元 + 置いた 2 体

    for (Actor* placed : {first.actor, second.actor})
    {
        CHECK(placed->name == "Coin");
        CHECK(placed->transform.position.x == 3.0f);
        CHECK(placed->GetScene() == scene);
        SpriteComponent* placedSprite = placed->GetComponent<SpriteComponent>();
        REQUIRE(placedSprite != nullptr);
        CHECK(placedSprite != sprite); // 元のコンポーネントを共有せず、新しく作っている
        CHECK(placedSprite->width == 24.0f);
        CHECK(placedSprite->color.g == doctest::Approx(0.8f));
        CHECK(placedSprite->GetOwner() == placed);
    }

    // 3 体とも guid が違う(プレハブには guid が入っておらず、置いたときに振られる)
    const ArchiveValue& actorProperties = *file.Find("objects")->At(0).Find("properties");
    CHECK(actorProperties.Find("guid") == nullptr);
    CHECK(first.actor->guid != original->guid);
    CHECK(second.actor->guid != original->guid);
    CHECK(first.actor->guid != second.actor->guid);

    // 置いたあとに片方を変えても、もう片方とプレハブは変わらない(コピーして作っているため)
    first.actor->GetComponent<SpriteComponent>()->width = 99.0f;
    CHECK(second.actor->GetComponent<SpriteComponent>()->width == 24.0f);
}

TEST_CASE("Prefab: アクターの外を指すハンドルは null にして警告を出す。置いたものは何も指さない")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* player = scene->Spawn<Actor>();
    Follower* follower = scene->Spawn<Follower>();
    follower->target = player->GetHandle();

    const Prefab::WriteResult saved = Prefab::Write(*follower);
    REQUIRE(saved.ok);
    CHECK(saved.warnings.size() == 1);

    const Prefab::InstantiateResult placed = Prefab::Instantiate(ThroughJson(saved.value), *scene, types);
    Follower* placedFollower = registry.Resolve<Follower>(placed.actor != nullptr ? placed.actor->GetHandle() : ObjectHandle{});
    REQUIRE(placedFollower != nullptr); // 型も Follower として作られている
    CHECK(placedFollower->target.IsNull());
}

TEST_CASE("Prefab: アクターが1つでない値は、何も作らずに断る。シーンにいないアクターからは作れない")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);

    ArchiveValue noActors = ArchiveValue::MakeObject();
    noActors.Set("version", ArchiveValue(std::int64_t{1}));
    noActors.Set("objects", ArchiveValue::MakeArray());
    noActors.Set("actors", ArchiveValue::MakeArray());
    const Prefab::InstantiateResult placed = Prefab::Instantiate(noActors, *scene, types);
    CHECK(placed.actor == nullptr);
    CHECK(placed.warnings.size() == 1);
    CHECK(scene->actors.empty());

    Actor* outside = registry.Create<Actor>(); // シーンに入れていない
    CHECK(!Prefab::Write(*outside).ok);
}

TEST_CASE("PrefabAsset: .prefab を GUID で読むと値の木が手に入り、そこから置ける。同じ GUID は同じアセット")
{
    husky::tests::TestFolder folder("husky_test_prefab_load");
    folder.Write("prefabs/coin.prefab", R"({ "version": 1, "objects": [
        { "id": 0, "type": "Actor", "properties": { "name": "Coin", "components": [1] } },
        { "id": 1, "type": "SpriteComponent", "properties": { "width": 24 } }
    ], "actors": [0] })");
    const husky::core::Guid guid = husky::tests::MakeGuid(0x5b1c2d3e4f504a6bull, 0x8c9d0e1f2a3b4c5dull);
    folder.Write("prefabs/coin.prefab.meta", husky::engine::AssetDatabase::MakeMetaText(guid));

    husky::engine::Application app; // ワーカー 0 本(同期実行)
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    husky::engine::AssetLoader& loader = app.GetAssetLoader();

    const ObjectHandle handle = loader.LoadPrefab(guid);
    const husky::engine::PrefabAsset* prefab = loader.ResolvePrefab(handle);
    REQUIRE(prefab != nullptr);
    CHECK(prefab->GetState() == husky::engine::AssetState::Loading); // すぐには読み終わらない(ジョブで読む)
    CHECK(loader.LoadPrefab(guid) == handle);                        // 読み込み中でも、同じアセットを返す

    loader.WaitForAllLoads();
    REQUIRE(prefab->IsLoaded());
    CHECK(loader.GetLoadedPrefabCount() == 1);

    const Prefab::InstantiateResult placed = Prefab::Instantiate(prefab->GetData(), app.GetScene(), app.GetTypeRegistry());
    CHECK(placed.warnings.empty());
    REQUIRE(placed.actor != nullptr);
    CHECK(placed.actor->name == "Coin");
    REQUIRE(placed.actor->GetComponent<SpriteComponent>() != nullptr);
    CHECK(placed.actor->GetComponent<SpriteComponent>()->width == 24.0f);

    CHECK(husky::engine::AssetLoader::CanLoadAs(husky::engine::PrefabAsset::StaticType(), "prefabs/coin.prefab"));
    CHECK_FALSE(husky::engine::AssetLoader::CanLoadAs(husky::engine::PrefabAsset::StaticType(), "sprites/a.spritesheet"));
}

TEST_CASE("PrefabAsset: JSON として読めないファイルや、表に無い GUID は「失敗」になる")
{
    husky::tests::TestFolder folder("husky_test_prefab_broken");
    const husky::core::Guid broken = husky::tests::MakeGuid(0x5b1c2d3e4f504a6bull, 0x8c9d0e1f2a3b4c5eull);
    folder.Write("broken.prefab", "{ not json");
    folder.Write("broken.prefab.meta", husky::engine::AssetDatabase::MakeMetaText(broken));

    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    husky::engine::AssetLoader& loader = app.GetAssetLoader();
    const husky::engine::PrefabAsset* brokenPrefab = loader.ResolvePrefab(loader.LoadPrefab(broken));
    const husky::engine::PrefabAsset* missing =
        loader.ResolvePrefab(loader.LoadPrefab(husky::tests::MakeGuid(1, 2)));
    loader.WaitForAllLoads();
    REQUIRE(brokenPrefab != nullptr);
    REQUIRE(missing != nullptr);
    CHECK(brokenPrefab->GetState() == husky::engine::AssetState::Failed);
    CHECK(missing->GetState() == husky::engine::AssetState::Failed);
}
