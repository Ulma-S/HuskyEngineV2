// ゲームのコード(src/game)のテスト。ゲームの exe とエディタが使う「型の登録」と「最初の配置」を確かめる。
#include "game/Game.h"

#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteComponent.h"
#include "game/GameActors.h"

#include <doctest/doctest.h>

using husky::engine::Application;

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
    husky::game::SpawnInitialActors(app.GetScene(), app.GetAssetDatabase());
    REQUIRE(app.GetScene().actors.size() == husky::game::BouncerCount + 2); // + カメラ + プレイヤー

    // カメラは画面の真ん中にいる
    const husky::engine::CameraComponent* camera = husky::engine::FindSceneCamera(app.GetScene());
    REQUIRE(camera != nullptr);
    CHECK(camera->GetOwner()->transform.position.x == doctest::Approx(husky::game::ScreenWidth / 2.0f));
    CHECK(camera->GetOwner()->transform.position.y == doctest::Approx(husky::game::ScreenHeight / 2.0f));

    // 一覧の最後(一番手前)がプレイヤー
    const auto* player = app.GetRegistry().Resolve<husky::game::PlayerActor>(app.GetScene().actors.back());
    REQUIRE(player != nullptr);
    CHECK(player->GetComponent<husky::engine::SpriteComponent>() != nullptr);

    // 乱数の種が固定なので、2回作っても最初の跳ね回るアクターは同じ位置
    Application other;
    husky::game::RegisterGameTypes(other.GetTypeRegistry());
    husky::game::SpawnInitialActors(other.GetScene(), other.GetAssetDatabase());
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
