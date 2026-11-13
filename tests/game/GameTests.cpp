// ゲームのコード(src/game)のテスト。ゲームの exe とエディタが使う「型の登録」と「最初の配置」を確かめる。
#include "game/Game.h"

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/Collider2D.h"
#include "engine/Rigidbody2D.h"
#include "game/CameraFollowComponent.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "platform/FileSystem.h"
#include "game/BouncerSettings.h"
#include "game/GameActors.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

using husky::engine::Application;

namespace
{
    // ビルドのたびに exe の隣へコピーされる assets で、アセットの表を作る(最初の配置がプレハブを読むため)。
    void ScanBuiltAssets(Application& app)
    {
        REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    }
}

TEST_CASE("Game: ゲームの型を登録すると、名前から作れる")
{
    Application app;
    CHECK(app.GetTypeRegistry().Find("BouncerActor") == nullptr);
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    CHECK(app.GetTypeRegistry().Find("BouncerActor") != nullptr);
    CHECK(app.GetTypeRegistry().Find("PlayerActor") != nullptr);
}

TEST_CASE("Game: 最初の配置は、跳ね回るアクター、カメラ、プレイヤー。毎回同じ配置で、保存して別のアプリで読み戻せる")
{
    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    husky::game::SpawnInitialActors(app);
    REQUIRE(app.GetScene().actors.size() == husky::game::BouncerCount + husky::game::CrateCount + husky::game::WallCount + 2); // + 木箱 + 壁 + カメラ + プレイヤー

    // 一覧の最後(一番手前)がプレイヤー
    const auto* player = app.GetRegistry().Resolve<husky::game::PlayerActor>(app.GetScene().actors.back());
    REQUIRE(player != nullptr);
    CHECK(player->GetComponent<husky::engine::SpriteComponent>() != nullptr);

    // プレハブから置いている: 元のプレハブを覚え、見た目の決め事(大きさ、スプライトシート、アニメーション)はプレハブのとおり
    const husky::core::Guid* playerPrefab = app.GetAssetDatabase().FindGuid("prefabs/player.prefab");
    REQUIRE(playerPrefab != nullptr);
    CHECK(player->prefab.guid == *playerPrefab);
    CHECK(player->name == "Player");
    CHECK(player->GetComponent<husky::engine::SpriteComponent>()->width == husky::game::PlayerSize);
    CHECK_FALSE(player->GetComponent<husky::engine::SpriteComponent>()->sheet.IsNull());
    REQUIRE(player->GetComponent<husky::engine::SpriteAnimatorComponent>() != nullptr);
    CHECK(player->GetComponent<husky::engine::SpriteAnimatorComponent>()->playing);
    const auto* bouncer = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors.front());
    REQUIRE(bouncer != nullptr);
    CHECK(bouncer->prefab.guid == *app.GetAssetDatabase().FindGuid("prefabs/bouncer.prefab"));
    CHECK(bouncer->GetComponent<husky::engine::SpriteComponent>()->width == husky::game::BouncerSize);
    CHECK(bouncer->vx != 0.0f); // 1つずつ違う値は、置いた後に書いている

    // カメラはプレイヤーを追いかけ、最初からプレイヤーの真ん中(= プレイヤーの位置。ピボットが真ん中)を映す位置にいる
    const husky::engine::CameraComponent* camera = husky::engine::FindSceneCamera(app.GetScene());
    REQUIRE(camera != nullptr);
    const auto* follow = camera->GetOwner()->GetComponent<husky::game::CameraFollowComponent>();
    REQUIRE(follow != nullptr);
    CHECK(follow->target == player->GetHandle());
    CHECK(camera->GetOwner()->transform.position.x ==
          doctest::Approx(player->transform.position.x));
    CHECK(camera->GetOwner()->transform.position.y ==
          doctest::Approx(player->transform.position.y));

    // 乱数の種が固定なので、2回作っても最初の跳ね回るアクターは同じ位置
    Application other;
    husky::game::RegisterGameTypes(other.GetTypeRegistry());
    ScanBuiltAssets(other);
    husky::game::SpawnInitialActors(other);
    const auto* first = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors.front());
    const auto* otherFirst = other.GetRegistry().Resolve<husky::game::BouncerActor>(other.GetScene().actors.front());
    REQUIRE(first != nullptr);
    REQUIRE(otherFirst != nullptr);
    CHECK(first->transform.position.x == otherFirst->transform.position.x);
    CHECK(first->vy == otherFirst->vy);

    // ゲームの型を登録したアプリなら、保存したシーンを読み戻せる(エディタがシーンを扱うときと同じ)
    const husky::engine::SceneSerializer::WriteResult saved = husky::engine::SceneSerializer::Write(app.GetScene());
    Application loaded;
    husky::game::RegisterGameTypes(loaded.GetTypeRegistry());
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved.value, loaded.GetScene(), loaded.GetTypeRegistry());
    CHECK(report.warnings.empty());
    CHECK(loaded.GetScene().actors.size() == app.GetScene().actors.size());
    // 読み戻しても、カメラはプレイヤーを指したまま(ハンドルは番号の対応表で保存される)
    const husky::engine::CameraComponent* loadedCamera = husky::engine::FindSceneCamera(loaded.GetScene());
    REQUIRE(loadedCamera != nullptr);
    const auto* loadedFollow = loadedCamera->GetOwner()->GetComponent<husky::game::CameraFollowComponent>();
    REQUIRE(loadedFollow != nullptr);
    CHECK(loaded.GetRegistry().Resolve<husky::game::PlayerActor>(loadedFollow->target) != nullptr);
}

TEST_CASE("Game: プレイヤーは上キーで +y(上)へ、下キーで -y(下)へ動く(y は上が +)")
{
    using husky::platform::Key;
    using husky::platform::KeyEvent;

    Application app;
    app.GetClock().SetFixedDelta(0.1);
    auto* player = app.GetScene().Spawn<husky::game::PlayerActor>();
    REQUIRE(player != nullptr);
    player->transform.position.y = 100.0f;

    // 上キーを押したまま 1 フレーム: 速さ 300 × 0.1 秒 = 30 だけ上へ
    app.StepFrame(0, {KeyEvent{Key::Up, true}});
    CHECK(player->transform.position.y == doctest::Approx(130.0f));

    // 上キーを離し、下キーを押したまま 1 フレーム: 30 だけ下へ戻る
    app.StepFrame(0, {KeyEvent{Key::Up, false}, KeyEvent{Key::Down, true}});
    CHECK(player->transform.position.y == doctest::Approx(100.0f));
}

TEST_CASE("Game: プレイヤーは世界の外へ出られない")
{
    using husky::platform::Key;
    using husky::platform::KeyEvent;

    Application app;
    app.GetClock().SetFixedDelta(0.1);
    auto* player = app.GetScene().Spawn<husky::game::PlayerActor>();
    REQUIRE(player != nullptr);
    const float half = husky::game::PlayerSize / 2.0f; // 位置はプレイヤーの真ん中なので、端から半分の大きさで止まる
    player->transform.position = {half + 5.0f, half + 5.0f, 0.0f};

    // 左下へ 30 動こうとしても、大きさの半分で止まる
    app.StepFrame(0, {KeyEvent{Key::Left, true}, KeyEvent{Key::Down, true}});
    CHECK(player->transform.position.x == half);
    CHECK(player->transform.position.y == half);

    // 右上の端は、世界の大きさ - プレイヤーの大きさの半分
    player->transform.position = {husky::game::WorldWidth - half - 10.0f, husky::game::WorldHeight - half - 10.0f, 0.0f};
    app.StepFrame(0, {KeyEvent{Key::Left, false}, KeyEvent{Key::Down, false}, KeyEvent{Key::Right, true},
                      KeyEvent{Key::Up, true}});
    CHECK(player->transform.position.x == doctest::Approx(husky::game::WorldWidth - half));
    CHECK(player->transform.position.y == doctest::Approx(husky::game::WorldHeight - half));
}

TEST_CASE("COL-4: プレハブのコライダーで、跳ね回るものはプレイヤーに触れている間だけ赤くなる(跳ね回るもの同士では変わらない)")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    husky::game::SpawnInitialActors(app);
    auto* player = app.GetRegistry().Resolve<husky::game::PlayerActor>(app.GetScene().actors.back());
    auto* bouncer = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors.front());
    auto* other = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors[1]);
    REQUIRE(player != nullptr);
    REQUIRE(bouncer != nullptr);
    REQUIRE(other != nullptr);
    // プレハブにコライダーが入っている(プレイヤーは円 48、跳ね回るものは円 16。グループは GameActors.h の決め事)
    REQUIRE(bouncer->GetComponent<husky::engine::CircleCollider2D>() != nullptr);
    CHECK(bouncer->GetComponent<husky::engine::CircleCollider2D>()->layer == husky::game::BouncerLayer);
    REQUIRE(player->GetComponent<husky::engine::CircleCollider2D>() != nullptr);
    CHECK(player->GetComponent<husky::engine::CircleCollider2D>()->layer == husky::game::PlayerLayer);

    // 2 つの跳ね回るものを、プレイヤーから離れた所で重ね、止める(跳ね回るもの同士は当たらない)
    bouncer->vx = bouncer->vy = 0.0f;
    other->vx = other->vy = 0.0f;
    bouncer->transform.position = {100.0f, 100.0f, 0.0f};
    other->transform.position = {105.0f, 100.0f, 0.0f};
    const husky::render::Color original = bouncer->GetComponent<husky::engine::SpriteComponent>()->color;
    app.StepFrame(0, {});
    CHECK(bouncer->GetComponent<husky::engine::SpriteComponent>()->color.r == original.r);
    CHECK(bouncer->touching == 0);

    // プレイヤーに重ねると赤くなる
    bouncer->transform.position = player->transform.position;
    app.StepFrame(0, {});
    CHECK(bouncer->touching == 1);
    CHECK(bouncer->GetComponent<husky::engine::SpriteComponent>()->color.r == husky::game::TouchedColor.r);
    CHECK(bouncer->GetComponent<husky::engine::SpriteComponent>()->color.g == husky::game::TouchedColor.g);

    // 離すと元の色に戻る
    bouncer->transform.position = {100.0f, 100.0f, 0.0f};
    app.StepFrame(0, {});
    CHECK(bouncer->touching == 0);
    CHECK(bouncer->GetComponent<husky::engine::SpriteComponent>()->color.g == original.g);
}

TEST_CASE("SD-4: 跳ね回るものの数・速さ・プレハブは設定のデータ(data/bouncers.sdata)から読み、置いたものは同じデータを指す")
{
    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    husky::game::SpawnInitialActors(app);

    // assets の data/bouncers.sdata が読め、置いたもの全員がプレハブの値としてそれを指す(1体ずつの値ではない)
    const husky::core::Guid* dataGuid = app.GetAssetDatabase().FindGuid("data/bouncers.sdata");
    REQUIRE(dataGuid != nullptr);
    auto* bouncer = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors.front());
    REQUIRE(bouncer != nullptr);
    CHECK(bouncer->settings.guid == *dataGuid);
    const husky::game::BouncerSettings* settings = app.GetAssetLoader().Resolve(bouncer->settings);
    REQUIRE(settings != nullptr);
    CHECK(app.GetScene().actors.size() == static_cast<std::size_t>(settings->count + husky::game::CrateCount + husky::game::WallCount) + 2); // + 木箱 + 壁 + カメラ + プレイヤー
    CHECK(settings->IsReadOnly());
}

TEST_CASE("SD-4: 設定のデータの count と maxSpeed を変えると、置く数と速さが変わる")
{
    // exe の隣の assets から、跳ね回るもののプレハブだけを写した assets を作る(プレイヤーのプレハブは無いので置かない)
    const std::string built = husky::platform::GetExecutableDirectory() + "assets/";
    std::string prefabText;
    std::string prefabMeta;
    REQUIRE(husky::platform::ReadTextFile((built + "prefabs/bouncer.prefab").c_str(), prefabText));
    REQUIRE(husky::platform::ReadTextFile((built + "prefabs/bouncer.prefab.meta").c_str(), prefabMeta));
    husky::core::Guid prefabGuid;
    REQUIRE(husky::engine::AssetDatabase::ParseMetaText(prefabMeta, prefabGuid));
    husky::tests::TestFolder folder("husky_test_bouncer_settings");
    folder.Write("prefabs/bouncer.prefab", prefabText);
    folder.Write("prefabs/bouncer.prefab.meta", prefabMeta);
    folder.Write("data/bouncers.sdata", R"({ "type": "BouncerSettings", "properties": { "count": 5, "maxSpeed": 10, "prefab": ")" +
                                            prefabGuid.ToString() + R"(" } })");
    folder.Write("data/bouncers.sdata.meta",
                 husky::engine::AssetDatabase::MakeMetaText(husky::tests::MakeGuid(0x7e1a2b3c4d5e4f60ull, 0x9a9b0c1d2e3f4a01ull)));

    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    husky::game::SpawnInitialActors(app);
    REQUIRE(app.GetScene().actors.size() == 5 + husky::game::WallCount + 1); // 跳ね回るもの 5 + 壁 + カメラ(プレイヤーと木箱のプレハブは無い)
    for (std::size_t i = 0; i < 5; ++i)
    {
        const auto* bouncer = app.GetRegistry().Resolve<husky::game::BouncerActor>(app.GetScene().actors[i]);
        REQUIRE(bouncer != nullptr);
        CHECK(bouncer->vx >= -10.0f);
        CHECK(bouncer->vx <= 10.0f);
        CHECK(bouncer->vy >= -10.0f);
        CHECK(bouncer->vy <= 10.0f);
    }
}

TEST_CASE("PHY-5: プレイヤー(Kinematic)で木箱(Dynamic)を押せる。木箱は世界の端の壁で止まる")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    husky::game::SpawnInitialActors(app);
    auto* player = app.GetRegistry().Resolve<husky::game::PlayerActor>(app.GetScene().actors.back());
    REQUIRE(player != nullptr);
    // プレイヤーはプレハブで Kinematic の体を持つ
    REQUIRE(player->GetComponent<husky::engine::Rigidbody2D>() != nullptr);
    CHECK(player->GetComponent<husky::engine::Rigidbody2D>()->bodyType == husky::engine::BodyType2D::Kinematic);

    // 木箱(名前 "Crate")を集める。最初の木箱は、プレイヤーの右 200、上 40 にある
    std::vector<husky::engine::Actor*> crates;
    int walls = 0;
    app.GetScene().ForEachActor([&](husky::engine::Actor& actor) {
        if (actor.name == "Crate")
        {
            crates.push_back(&actor);
        }
        walls += actor.name.rfind("Wall", 0) == 0 ? 1 : 0;
    });
    REQUIRE(crates.size() == static_cast<std::size_t>(husky::game::CrateCount));
    CHECK(walls == 4);
    husky::engine::Actor* first = crates[0];
    const float startX = first->transform.position.x;
    REQUIRE(first->GetComponent<husky::engine::Rigidbody2D>() != nullptr);

    // プレイヤーを木箱の高さに合わせ、右へ 1 フレームに 5 ずつ 1 秒動かす(PlayerActor::Tick と同じく、コードが位置を書く)
    player->transform.position.y = first->transform.position.y;
    app.StepFrame(0, {});
    for (int i = 0; i < 60; ++i)
    {
        player->transform.position.x += 5.0f;
        app.StepFrame(0, {});
    }
    // 押された木箱は、プレイヤーの右側(円の半径 48 + 箱の半分 32)より先にいる
    CHECK(first->transform.position.x > startX + 50.0f);
    CHECK(first->transform.position.x >= player->transform.position.x + 48.0f + 32.0f - 2.0f);

    // 木箱を世界の右の端の向こうへ押し出そうとしても、壁で止まる(速さを付けて壁へ向かわせる)
    first->transform.position = {husky::game::WorldWidth - 100.0f, 100.0f, 0.0f};
    first->GetComponent<husky::engine::Rigidbody2D>()->SetLinearVelocity({2000.0f, 0.0f});
    for (int i = 0; i < 60; ++i)
    {
        app.StepFrame(0, {});
    }
    CHECK(first->transform.position.x <= husky::game::WorldWidth - 32.0f + 1.0f);
}
