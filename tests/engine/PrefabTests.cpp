// プレハブ(engine/Prefab.h)のテスト。アクターから作り、JSON を通して、何度でも置けることを確かめる。
#include "core/ObjectRegistry.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"
#include "engine/CameraComponent.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <string>
#include <utility> // std::swap
#include <vector>

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

    // プレハブのコンポーネントの ID(テストでは決まった値にする。プレハブを書き直しても、同じコンポーネントは同じ ID のまま)。
    const husky::core::Guid SpriteId = husky::tests::MakeGuid(0x1111111111114111ull, 0x8111111111111111ull);
    const husky::core::Guid AnimatorId = husky::tests::MakeGuid(0x2222222222224222ull, 0x8222222222222222ull);

    // 値の木を写す。名前が key のメンバーは make(元の値) の値に置き換え、make が Null を返せば取り除く(中まですべて)。
    ArchiveValue Rewrite(const ArchiveValue& value, std::string_view key,
                         const std::function<ArchiveValue(const ArchiveValue&)>& make)
    {
        if (value.GetType() == ArchiveValue::Type::Array)
        {
            ArchiveValue copy = ArchiveValue::MakeArray();
            for (std::size_t i = 0; i < value.GetArraySize(); ++i)
            {
                copy.Append(Rewrite(value.At(i), key, make));
            }
            return copy;
        }
        if (value.GetType() != ArchiveValue::Type::Object)
        {
            return value;
        }
        ArchiveValue copy = ArchiveValue::MakeObject();
        for (std::size_t i = 0; i < value.GetMemberCount(); ++i)
        {
            const std::string& name = value.GetMemberName(i);
            if (name != key)
            {
                copy.Set(name, Rewrite(value.GetMemberValue(i), key, make));
                continue;
            }
            ArchiveValue replaced = make(value.GetMemberValue(i));
            if (!replaced.IsNull())
            {
                copy.Set(name, std::move(replaced));
            }
        }
        return copy;
    }

    // PF-4 のテストで使うプレハブ(Actor + 幅 width、高さ 16 の SpriteComponent)を folder に書く。名前と幅を変えて書き直せる。
    // withAnimator なら、2つ目のコンポーネントとして SpriteAnimatorComponent も付ける(PF-7a: プレハブに後から足す)。
    // prefab.create と同じく、アクターから Prefab::Write で作る(全部の値が入る。違いは、書いてある値とだけ比べられるため)。
    // コンポーネントの ID は、Prefab::Write が振った乱数を SpriteId / AnimatorId に替える(書き直しても同じ ID にするため)。
    void WriteBoxPrefab(husky::tests::TestFolder& folder, const char* name, float width, bool withAnimator = false)
    {
        husky::engine::Application maker;
        Actor* actor = maker.GetScene().Spawn<Actor>();
        REQUIRE(actor != nullptr);
        actor->name = name;
        SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
        REQUIRE(sprite != nullptr);
        sprite->width = width;
        sprite->height = 16.0f;
        if (withAnimator)
        {
            REQUIRE(actor->AddComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
        }
        const Prefab::WriteResult written = Prefab::Write(*actor);
        REQUIRE(written.ok);
        std::size_t count = 0; // コンポーネントは並んだ順に出てくる
        const ArchiveValue fixedIds = Rewrite(written.value, "prefabComponentId", [&count](const ArchiveValue&) {
            return ArchiveValue((count++ == 0 ? SpriteId : AnimatorId).ToString());
        });
        std::string text;
        REQUIRE(JsonFormat::Write(fixedIds, text));
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
    // コンポーネント: 変えた height と、いつも書く prefabComponentId(プレハブのどれと組にするか)だけ
    const ArchiveValue& sprite = PropertiesAt(saved, 1);
    CHECK(sprite.GetMemberCount() == 2);
    REQUIRE(sprite.Find("prefabComponentId") != nullptr);
    CHECK(sprite.Find("prefabComponentId")->AsString() == SpriteId.ToString());
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
    CHECK(PropertiesAt(again, 1).GetMemberCount() == 2);
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

namespace
{
    // シーンの値の木を JSON に通してから、別のアプリに読む(プレハブは folder から探す)。警告は CHECK で 0 件を確かめる。
    const Actor* ReadInto(husky::engine::Application& app, const husky::tests::TestFolder& folder, const ArchiveValue& saved)
    {
        REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
        const husky::engine::SceneSerializer::ReadReport report =
            husky::engine::SceneSerializer::Read(ThroughJson(saved), app.GetScene(), app.GetTypeRegistry(), FinderOf(app));
        CHECK(report.warnings.empty());
        REQUIRE(report.addedActors.size() == 1);
        return app.GetRegistry().Resolve<Actor>(report.addedActors[0]);
    }
}

TEST_CASE("PF-7a: 置いた後に足したコンポーネントは全部の値を書き、外したものは removedComponents に ID を書く。読むとそのとおりに戻る")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff_added");
    WriteBoxPrefab(folder, "Box", 32.0f, true); // スプライト + アニメーター

    // 置いて、アニメーターを外し、カメラを足す
    ArchiveValue saved;
    {
        husky::engine::Application app;
        Actor* placed = PlaceBox(app, folder);
        placed->GetComponent<husky::engine::SpriteAnimatorComponent>()->Destroy();
        app.GetScene().RemoveDestroyedActors(); // フレームの終わり: Destroy 済みのコンポーネントが一覧から外れる
        husky::engine::CameraComponent* camera = placed->AddComponent<husky::engine::CameraComponent>();
        REQUIRE(camera != nullptr);
        camera->zoom = 2.0f;
        const husky::engine::SceneSerializer::WriteResult written = husky::engine::SceneSerializer::Write(app.GetScene());
        CHECK(written.warnings.empty());
        saved = ThroughJson(written.value);
    }

    // アクターの記録に、外したもの(アニメーター)の ID。アクターは違いだけ(name は書かない)
    const ArchiveValue& actorEntry = saved.Find("objects")->At(0);
    REQUIRE(actorEntry.Find("removedComponents") != nullptr);
    REQUIRE(actorEntry.Find("removedComponents")->GetArraySize() == 1);
    CHECK(actorEntry.Find("removedComponents")->At(0).AsString() == AnimatorId.ToString());
    CHECK(PropertiesAt(saved, 0).Find("name") == nullptr);
    // スプライト(組になる): ID だけ。カメラ(足したもの): 全部の値
    CHECK(PropertiesAt(saved, 1).GetMemberCount() == 1);
    CHECK(PropertiesAt(saved, 2).Find("zoom") != nullptr);

    husky::engine::Application other;
    const Actor* loaded = ReadInto(other, folder, saved);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->name == "Box");
    CHECK(loaded->components.size() == 2);
    CHECK(loaded->GetComponent<husky::engine::SpriteAnimatorComponent>() == nullptr);
    REQUIRE(loaded->GetComponent<SpriteComponent>() != nullptr);
    CHECK(loaded->GetComponent<SpriteComponent>()->prefabComponentId == SpriteId);
    CHECK(loaded->GetComponent<SpriteComponent>()->width == 32.0f);
    REQUIRE(loaded->GetComponent<husky::engine::CameraComponent>() != nullptr);
    CHECK(loaded->GetComponent<husky::engine::CameraComponent>()->zoom == 2.0f);
    CHECK(loaded->GetComponent<husky::engine::CameraComponent>()->GetOwner() == loaded);

    // もう一度保存しても、同じ形(外した一覧が残る)
    const ArchiveValue again = husky::engine::SceneSerializer::Write(other.GetScene()).value;
    REQUIRE(again.Find("objects")->At(0).Find("removedComponents") != nullptr);
    CHECK(again.Find("objects")->At(0).Find("removedComponents")->GetArraySize() == 1);
}

TEST_CASE("PF-7a: プレハブに後から足したコンポーネントは、置いたものにも伝わる(外した一覧に無いので)")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff_grow");
    WriteBoxPrefab(folder, "Box", 32.0f); // スプライトだけ

    ArchiveValue saved;
    {
        husky::engine::Application app;
        PlaceBox(app, folder)->GetComponent<SpriteComponent>()->height = 20.0f;
        saved = ThroughJson(husky::engine::SceneSerializer::Write(app.GetScene()).value);
    }
    CHECK(saved.Find("objects")->At(0).Find("removedComponents") == nullptr);

    WriteBoxPrefab(folder, "Box", 32.0f, true); // プレハブにアニメーターを足す
    husky::engine::Application other;
    const Actor* loaded = ReadInto(other, folder, saved);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->components.size() == 2);
    REQUIRE(loaded->GetComponent<SpriteComponent>() != nullptr);
    CHECK(loaded->GetComponent<SpriteComponent>()->height == 20.0f); // 違いは残る
    REQUIRE(loaded->GetComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
    CHECK(loaded->GetComponent<husky::engine::SpriteAnimatorComponent>()->GetOwner() == loaded);
}

TEST_CASE("PF-7a: コンポーネントに ID の無い古いシーンのファイルは、並び順でプレハブと組にする")
{
    husky::tests::TestFolder folder("husky_test_prefab_diff_legacy");
    WriteBoxPrefab(folder, "Box", 32.0f);

    ArchiveValue saved;
    {
        husky::engine::Application app;
        PlaceBox(app, folder)->GetComponent<SpriteComponent>()->height = 20.0f;
        saved = husky::engine::SceneSerializer::Write(app.GetScene()).value;
    }
    // ID を取り除き、PF-4 の形にする
    const ArchiveValue legacy = Rewrite(saved, "prefabComponentId", [](const ArchiveValue&) { return ArchiveValue(); });
    CHECK(PropertiesAt(legacy, 1).GetMemberCount() == 1);

    husky::engine::Application other;
    const Actor* loaded = ReadInto(other, folder, legacy);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->components.size() == 1);
    const SpriteComponent* sprite = loaded->GetComponent<SpriteComponent>();
    REQUIRE(sprite != nullptr);
    CHECK(sprite->height == 20.0f);
    CHECK(sprite->prefabComponentId == SpriteId); // プレハブから作ったので、ID はプレハブのもの(次に保存すると ID の形になる)
}

TEST_CASE("PF-7b: FindOverrides はコンポーネントを ID で組にし、足した・外したものを並べる。値の違いは組になったものだけ")
{
    husky::tests::TestFolder folder("husky_test_prefab_overrides_ids");
    WriteBoxPrefab(folder, "Box", 32.0f, true); // スプライト + アニメーター

    husky::engine::Application app;
    Actor* placed = PlaceBox(app, folder);
    placed->GetComponent<SpriteComponent>()->height = 20.0f;
    placed->GetComponent<husky::engine::SpriteAnimatorComponent>()->Destroy();
    app.GetScene().RemoveDestroyedActors();
    REQUIRE(placed->AddComponent<husky::engine::CameraComponent>() != nullptr);

    const Prefab::OverridesResult found = Prefab::FindOverrides(*placed);
    REQUIRE(found.prefab != nullptr);
    CHECK(found.error.empty());
    REQUIRE(found.overrides.size() == 1);
    CHECK(found.overrides[0].component == "SpriteComponent");
    CHECK(found.overrides[0].path == "height");
    CHECK(found.overrides[0].componentId == SpriteId);
    REQUIRE(found.addedComponents.size() == 1);
    CHECK(found.addedComponents[0].type == "CameraComponent");
    REQUIRE(found.removedComponents.size() == 1);
    CHECK(found.removedComponents[0].type == "SpriteAnimatorComponent");
}

TEST_CASE("PF-7b: WriteChanges と FollowChanges は ID でコンポーネントを指す(同じ型を足したものと取り違えない)")
{
    husky::tests::TestFolder folder("husky_test_prefab_follow_ids");
    WriteBoxPrefab(folder, "Box", 32.0f);

    husky::engine::Application app;
    Actor* placed = PlaceBox(app, folder);
    // 同じ型のスプライトを足し、プレハブのものより前に並べる(型の名前で「最初のもの」を探すと、こちらを取り違える)
    SpriteComponent* added = placed->AddComponent<SpriteComponent>();
    REQUIRE(added != nullptr);
    added->width = 32.0f;
    REQUIRE(placed->components.size() == 2);
    std::swap(placed->components[0], placed->components[1]);
    SpriteComponent* fromPrefab = app.GetRegistry().Resolve<SpriteComponent>(placed->components[1]);
    REQUIRE(fromPrefab != nullptr);
    REQUIRE(fromPrefab->prefabComponentId == SpriteId);

    // プレハブの幅を 32 → 48 にしたことを伝える: プレハブから来たものだけが 48 になる
    const std::vector<Prefab::ValueChange> changes = {
        {"SpriteComponent", "width", ArchiveValue(32.0), ArchiveValue(48.0), SpriteId}};
    CHECK(Prefab::FollowChanges(*placed, changes) == 1);
    CHECK(fromPrefab->width == 48.0f);
    CHECK(added->width == 32.0f);

    // プレハブの値の木にも、ID の合うコンポーネントに書く
    const husky::engine::PrefabAsset* prefab = app.FindPrefabForScene(BoxGuid);
    REQUIRE(prefab != nullptr);
    ArchiveValue written;
    std::string error;
    REQUIRE(Prefab::WriteChanges(prefab->GetData(), changes, written, error));
    CHECK(PropertiesAt(written, 1).Find("width")->AsFloat() == 48.0);
    // 無い ID を指すと断る
    const std::vector<Prefab::ValueChange> wrong = {
        {"SpriteComponent", "width", ArchiveValue(32.0), ArchiveValue(48.0), AnimatorId}};
    CHECK_FALSE(Prefab::WriteChanges(prefab->GetData(), wrong, written, error));
}

TEST_CASE("COL-4: コンポーネントの ID の無い古いシーンでも、プレハブの末尾に後から足したコンポーネントは、先頭を並び順で組にして足される")
{
    husky::tests::TestFolder folder("husky_test_prefab_old_file_prefix");
    WriteBoxPrefab(folder, "Box", 32.0f, false); // スプライトだけ

    // 置いて幅を 48 に変え、保存する。ID を消して、PF-7a より前の古いファイルと同じ形にする
    ArchiveValue saved;
    {
        husky::engine::Application app;
        Actor* placed = PlaceBox(app, folder);
        placed->GetComponent<SpriteComponent>()->width = 48.0f;
        const ArchiveValue written = husky::engine::SceneSerializer::Write(app.GetScene()).value;
        saved = Rewrite(ThroughJson(written), "prefabComponentId", [](const ArchiveValue&) { return ArchiveValue(); });
    }

    // プレハブの末尾にアニメーターを足してから読む: 警告なしで、スプライトは違い(幅 48)のまま、アニメーターも付く
    WriteBoxPrefab(folder, "Box", 32.0f, true);
    husky::engine::Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved, app.GetScene(), app.GetTypeRegistry(), FinderOf(app));
    CHECK(report.warnings.empty());
    REQUIRE(app.GetScene().actors.size() == 1);
    Actor* actor = app.GetRegistry().Resolve<Actor>(app.GetScene().actors[0]);
    REQUIRE(actor != nullptr);
    CHECK(actor->components.size() == 2);
    REQUIRE(actor->GetComponent<SpriteComponent>() != nullptr);
    CHECK(actor->GetComponent<SpriteComponent>()->width == 48.0f);
    CHECK(actor->GetComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
}
