// ゲームのコード(src/game)のテスト。ゲームの exe とエディタが使う「型の登録」と「最初の配置」を確かめる。
#include "game/Game.h"

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "game/CameraFollowComponent.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "platform/FileSystem.h"
#include "game/GameActors.h"

#include <doctest/doctest.h>

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
    REQUIRE(app.GetScene().actors.size() == husky::game::BouncerCount + 2); // + カメラ + プレイヤー

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
