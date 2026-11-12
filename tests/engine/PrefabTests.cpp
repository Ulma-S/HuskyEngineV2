// プレハブ(engine/Prefab.h)のテスト。アクターから作り、JSON を通して、何度でも置けることを確かめる。
#include "core/ObjectRegistry.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"
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

namespace
{
    const husky::core::Guid BoxGuid = husky::tests::MakeGuid(0x1a2b3c4d5e6f4a1bull, 0x9c8d7e6f5a4b3c2dull);

    // PF-4 のテストで使うプレハブ(Actor + 幅 width、高さ 16 の SpriteComponent)を folder に書く。名前と幅を変えて書き直せる。
    // prefab.create と同じく、アクターから Prefab::Write で作る(全部の値が入る。違いは、書いてある値とだけ比べられるため)。
    void WriteBoxPrefab(husky::tests::TestFolder& folder, const char* name, float width)
    {
        husky::engine::Application maker;
        Actor* actor = maker.GetScene().Spawn<Actor>();
        REQUIRE(actor != nullptr);
        actor->name = name;
        SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
        REQUIRE(sprite != nullptr);
        sprite->width = width;
        sprite->height = 16.0f;
        const Prefab::WriteResult written = Prefab::Write(*actor);
        REQUIRE(written.ok);
        std::string text;
        REQUIRE(JsonFormat::Write(written.value, text));
        folder.Write("prefabs/box.prefab", text);
        folder.Write("prefabs/box.prefab.meta", husky::engine::AssetDatabase::MakeMetaText(BoxGuid));
    }

    // シーンを読むときと同じやり方(Application::FindPrefabForScene)でプレハブを探す関数。
    husky::engine::SceneSerializer::PrefabFinder FinderOf(husky::engine::Application& app)
    {
        return [&app](const husky::core::Guid& guid) { return app.FindPrefabForScene(guid); };
    }

    // folder でアセットの表を作ったアプリに、箱のプレハブを1つ置く。
    Actor* PlaceBox(husky::engine::Application& app, const husky::tests::TestFolder& folder)
    {
        REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
        const husky::engine::PrefabAsset* prefab = app.FindPrefabForScene(BoxGuid);
        REQUIRE(prefab != nullptr);
        const Prefab::InstantiateResult placed = Prefab::Instantiate(*prefab, app.GetScene(), app.GetTypeRegistry());
        REQUIRE(placed.actor != nullptr);
        return placed.actor;
    }

    // シーンの値の木の、index 番目のオブジェクトの "properties"。
    const ArchiveValue& PropertiesAt(const ArchiveValue& scene, std::size_t index)
    {
        return *scene.Find("objects")->At(index).Find("properties");
    }
}

TEST_CASE("PF-4: プレハブから置いたものは違いだけを保存する。読み直すとプレハブの値に違いが重なり、プレハブを直すと伝わる")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff");
    WriteBoxPrefab(folder, "Box", 32.0f);

    // 置いて、2つだけ値を変える(位置の x と、スプライトの高さ)
    ArchiveValue saved;
    {
        husky::engine::Application app;
        Actor* placed = PlaceBox(app, folder);
        placed->transform.position.x = 5.0f;
        placed->GetComponent<SpriteComponent>()->height = 20.0f;
        const husky::engine::SceneSerializer::WriteResult written = husky::engine::SceneSerializer::Write(app.GetScene());
        CHECK(written.warnings.empty());
        saved = ThroughJson(written.value);
    }

    // アクター: 変えた position.x と、いつも書くもの(guid、prefab、components)だけ。name や position.y は書かない
    const ArchiveValue& actor = PropertiesAt(saved, 0);
    CHECK(actor.Find("guid") != nullptr);
    REQUIRE(actor.Find("prefab") != nullptr);
    CHECK(actor.Find("prefab")->AsString() == BoxGuid.ToString());
    CHECK(actor.Find("components") != nullptr);
    CHECK(actor.Find("name") == nullptr);
    REQUIRE(actor.Find("transform") != nullptr);
    CHECK(actor.Find("transform")->GetMemberCount() == 1); // rotation、scale は同じなので書かない
    REQUIRE(actor.Find("transform")->Find("position") != nullptr);
    CHECK(actor.Find("transform")->Find("position")->GetMemberCount() == 1);
    CHECK(actor.Find("transform")->Find("position")->Find("x")->AsFloat() == 5.0);
    // コンポーネント: 変えた height だけ
    const ArchiveValue& sprite = PropertiesAt(saved, 1);
    CHECK(sprite.GetMemberCount() == 1);
    REQUIRE(sprite.Find("height") != nullptr);
    CHECK(sprite.Find("height")->AsFloat() == 20.0);

    // プレハブを直す(名前と幅)。違いの無い値は、直したプレハブの値になる
    WriteBoxPrefab(folder, "Crate", 64.0f);
    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved, app.GetScene(), app.GetTypeRegistry(), FinderOf(app));
    CHECK(report.warnings.empty());
    CHECK(report.createdCount == 2);
    REQUIRE(report.addedActors.size() == 1);
    const Actor* loaded = app.GetRegistry().Resolve<Actor>(report.addedActors[0]);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->name == "Crate");              // プレハブから(直した値)
    CHECK(loaded->transform.position.x == 5.0f); // ファイルの違いから
    CHECK(loaded->transform.position.y == 0.0f);
    CHECK(loaded->guid.ToString() == actor.Find("guid")->AsString());
    REQUIRE(loaded->GetComponent<SpriteComponent>() != nullptr);
    CHECK(loaded->GetComponent<SpriteComponent>()->width == 64.0f);  // プレハブから(直した値)
    CHECK(loaded->GetComponent<SpriteComponent>()->height == 20.0f); // ファイルの違いから
    CHECK(app.GetAssetLoader().ResolvePrefab(loaded->prefab.asset) != nullptr); // 読んだプレハブを覚えている

    // もう一度保存しても、違いは同じ(プレハブの値は書かない)
    const ArchiveValue again = husky::engine::SceneSerializer::Write(app.GetScene()).value;
    CHECK(PropertiesAt(again, 0).Find("name") == nullptr);
    CHECK(PropertiesAt(again, 1).GetMemberCount() == 1);
}

TEST_CASE("PF-4: プレハブが読めないときは、警告を出してファイルにある値だけで作る")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff_missing");
    WriteBoxPrefab(folder, "Box", 32.0f);

    ArchiveValue saved;
    {
        husky::engine::Application app;
        Actor* placed = PlaceBox(app, folder);
        placed->GetComponent<SpriteComponent>()->height = 20.0f;
        saved = ThroughJson(husky::engine::SceneSerializer::Write(app.GetScene()).value);
    }

    husky::engine::Application app; // アセットの表を作らない(プレハブが見つからない)
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved, app.GetScene(), app.GetTypeRegistry(), FinderOf(app));
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("読めない") != std::string::npos);
    REQUIRE(report.addedActors.size() == 1);
    const Actor* loaded = app.GetRegistry().Resolve<Actor>(report.addedActors[0]);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->prefab.guid == BoxGuid); // つながりの印は残す(プレハブが戻れば、次に読むときに使える)
    REQUIRE(loaded->GetComponent<SpriteComponent>() != nullptr);
    CHECK(loaded->GetComponent<SpriteComponent>()->height == 20.0f); // ファイルにある値
}

TEST_CASE("PF-4: コンポーネントを足したものは、警告を出して全部の値を書き、読むときもファイルの値だけで作る")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff_added");
    WriteBoxPrefab(folder, "Box", 32.0f);

    husky::engine::Application app;
    Actor* placed = PlaceBox(app, folder);
    REQUIRE(placed->AddComponent<SpriteComponent>() != nullptr); // プレハブに無い2つ目

    const husky::engine::SceneSerializer::WriteResult written = husky::engine::SceneSerializer::Write(app.GetScene());
    REQUIRE(written.warnings.size() == 1);
    CHECK(written.warnings[0].find("コンポーネントの並び") != std::string::npos);
    CHECK(PropertiesAt(written.value, 0).Find("name") != nullptr); // 全部の値
    CHECK(PropertiesAt(written.value, 1).Find("width") != nullptr);

    husky::engine::Application other;
    REQUIRE(other.GetAssetDatabase().Scan(folder.Root()));
    const husky::engine::SceneSerializer::ReadReport report = husky::engine::SceneSerializer::Read(
        ThroughJson(written.value), other.GetScene(), other.GetTypeRegistry(), FinderOf(other));
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("コンポーネントの並び") != std::string::npos);
    REQUIRE(report.addedActors.size() == 1);
    const Actor* loaded = other.GetRegistry().Resolve<Actor>(report.addedActors[0]);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->name == "Box");
    CHECK(loaded->components.size() == 2);
}
