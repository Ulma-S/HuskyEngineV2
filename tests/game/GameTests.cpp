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
#include "game/HudComponent.h"
#include "engine/UIActor.h"
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
    REQUIRE(app.GetScene().actors.size() == husky::game::BouncerCount + husky::game::CrateCount + husky::game::WallCount + 3); // + 木箱 + 壁 + カメラ + BGM + プレイヤー

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
    CHECK(app.GetScene().actors.size() == static_cast<std::size_t>(settings->count + husky::game::CrateCount + husky::game::WallCount) + 3); // + 木箱 + 壁 + カメラ + BGM + プレイヤー
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

TEST_CASE("Game: 左ボタンでクリックした所へ、プレイヤーが歩いていく。キーで動かすとやめる(UI-1)")
{
    using husky::platform::InputEvents;
    using husky::platform::Key;
    using husky::platform::KeyEvent;
    using husky::platform::MouseEvent;
    using husky::platform::MouseEventType;

    Application app;
    app.GetClock().SetFixedDelta(0.1);
    auto* player = app.GetScene().Spawn<husky::game::PlayerActor>();
    REQUIRE(player != nullptr);
    player->transform.position = husky::core::Vector3{500.0f, 100.0f, 0.0f};

    // カメラが無いので、画面ちょうどのカメラ(左下が原点、1 単位 = 1 ピクセル)。
    // 画面の (200, 620) は、y を裏返してワールドの (200, 720 - 620) = (200, 100)。
    InputEvents click;
    click.screenWidth = 1280.0f;
    click.screenHeight = 720.0f;
    MouseEvent down;
    down.type = MouseEventType::ButtonDown;
    down.x = 200.0f;
    down.y = 620.0f;
    click.mouse.push_back(down);

    InputEvents none; // イベントの無いフレーム(画面の大きさだけ)
    none.screenWidth = 1280.0f;
    none.screenHeight = 720.0f;

    // 押したフレームから歩き出す: 速さ 300 × 0.1 秒 = 30 ずつ左へ
    app.StepFrame(0, click);
    CHECK(player->transform.position.x == doctest::Approx(470.0f));
    CHECK(player->transform.position.y == doctest::Approx(100.0f));

    // 300 離れているので、10 フレーム目でぴったり着いて止まる
    for (int i = 0; i < 9; ++i)
    {
        app.StepFrame(0, none);
    }
    CHECK(player->transform.position.x == doctest::Approx(200.0f));
    app.StepFrame(0, none);
    CHECK(player->transform.position.x == doctest::Approx(200.0f));

    // もう一度クリックして、キーで動かすと、行き先はやめる(キーを離しても行き先へは戻らない)
    // (前のクリックのボタンをまだ離していないので、離してから押す。押したままだと「押した瞬間」にならない)
    MouseEvent up0 = click.mouse[0];
    up0.type = MouseEventType::ButtonUp;
    click.mouse.insert(click.mouse.begin(), up0);
    click.mouse[1].x = 600.0f;
    app.StepFrame(0, click); // 200 → 230
    InputEvents up = none;
    up.keys.push_back(KeyEvent{Key::Up, true});
    app.StepFrame(0, up); // 上へ 30
    CHECK(player->transform.position.x == doctest::Approx(230.0f));
    CHECK(player->transform.position.y == doctest::Approx(130.0f));
    InputEvents release = none;
    release.keys.push_back(KeyEvent{Key::Up, false});
    app.StepFrame(0, release);
    CHECK(player->transform.position.x == doctest::Approx(230.0f));
    CHECK(player->transform.position.y == doctest::Approx(130.0f));
}

TEST_CASE("Game: マウスの下のワールドの位置は、シーンのカメラで直す。画面の大きさが無ければ求めない(UI-1)")
{
    using husky::platform::InputEvents;
    using husky::platform::MouseEvent;

    Application app;
    husky::core::Vector3 world{-1.0f, -1.0f, 0.0f};

    // 画面の大きさを渡していない(テストや CLI): 求めず、outWorld も変えない
    app.StepFrame(0, InputEvents{});
    CHECK_FALSE(husky::engine::FindMouseWorldPosition(app.GetScene(), world));
    CHECK(world.x == -1.0f);

    // カメラを (1000, 2000) に置く(zoom 1): 画面の真ん中 (640, 360) がカメラの位置に映る。
    // 画面で右へ 100・下へ 50 は、ワールドで右へ 100・下へ 50(y は -)
    auto* cameraActor = app.GetScene().Spawn<husky::engine::Actor>();
    REQUIRE(cameraActor != nullptr);
    cameraActor->transform.position = husky::core::Vector3{1000.0f, 2000.0f, 0.0f};
    REQUIRE(cameraActor->AddComponent<husky::engine::CameraComponent>() != nullptr);

    InputEvents events;
    events.screenWidth = 1280.0f;
    events.screenHeight = 720.0f;
    MouseEvent move; // 既定の種類は Move
    move.x = 740.0f;
    move.y = 410.0f;
    events.mouse.push_back(move);
    app.StepFrame(0, events);
    REQUIRE(husky::engine::FindMouseWorldPosition(app.GetScene(), world));
    CHECK(world.x == doctest::Approx(1100.0f));
    CHECK(world.y == doctest::Approx(1950.0f));
}

// ---- HUD(UI-7) ----

namespace
{
    // シーンで最初の T のコンポーネント
    template <typename T>
    T* FindFirstComponent(Application& app)
    {
        T* found = nullptr;
        app.GetScene().ForEachActor([&](husky::engine::Actor& actor) {
            if (found == nullptr)
            {
                found = actor.GetComponent<T>();
            }
        });
        return found;
    }

    husky::engine::Actor* FindActorNamed(Application& app, const char* name)
    {
        husky::engine::Actor* found = nullptr;
        app.GetScene().ForEachActor([&](husky::engine::Actor& actor) {
            if (found == nullptr && actor.name == name)
            {
                found = &actor;
            }
        });
        return found;
    }
}

TEST_CASE("Game: Canvas の無いシーンには HUD のプレハブを置く。もう Canvas があれば置かない")
{
    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    husky::game::SpawnHudIfMissing(app);
    REQUIRE(FindFirstComponent<husky::engine::CanvasComponent>(app) != nullptr);
    REQUIRE(FindFirstComponent<husky::game::HudComponent>(app) != nullptr);
    CHECK(FindActorNamed(app, "ResetButton") != nullptr);
    const std::size_t count = app.GetScene().actors.size();
    CHECK(count == 5); // HUD、スコア、残り時間、ボタン、ボタンの文字

    husky::game::SpawnHudIfMissing(app); // 2回目は置かない
    CHECK(app.GetScene().actors.size() == count);
}

TEST_CASE("Game: HUD はプレイヤーのスコアと残り時間を文字に書き、時間切れで止まり、リセットのボタンで最初に戻る")
{
    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    ScanBuiltAssets(app);
    auto* player = app.GetScene().Spawn<husky::game::PlayerActor>();
    REQUIRE(player != nullptr);
    husky::game::SpawnHudIfMissing(app);
    husky::game::HudComponent* hud = FindFirstComponent<husky::game::HudComponent>(app);
    REQUIRE(hud != nullptr);
    husky::engine::UITextComponent* scoreText = hud->scoreText.Resolve(app.GetRegistry());
    husky::engine::UITextComponent* timeText = hud->timeText.Resolve(app.GetRegistry());
    REQUIRE(scoreText != nullptr);
    REQUIRE(timeText != nullptr);

    app.GetClock().SetFixedDelta(0.1); // dt は上限で切られるので、小さく進める
    app.StepFrame(0, {}); // 残り 59.9 秒(切り上げて 60 秒 = 01:00)
    CHECK(timeText->text == "残り時間: 01:00");
    CHECK(scoreText->text == "スコア: 0");

    player->score += 3; // 跳ね回るものに 3 回触れた
    for (int i = 0; i < 10; ++i) // 1 秒進めて、残り 58.9 秒(切り上げて 59 秒)
    {
        app.StepFrame(0, {});
    }
    CHECK(scoreText->text == "スコア: 3");
    CHECK(timeText->text == "残り時間: 00:59");

    // 60 秒を過ぎるまで進める
    for (int i = 0; i < 600 && !hud->IsFinished(); ++i)
    {
        app.StepFrame(0, {});
    }
    REQUIRE(hud->IsFinished());
    CHECK(timeText->text == "おしまい!");
    player->score += 5; // 時間切れの後は数えない
    app.StepFrame(0, {});
    CHECK(scoreText->text == "スコア: 3");

    // リセットのボタン(プレハブの中で HudComponent::ResetGame を指している)を押す
    husky::engine::Actor* button = FindActorNamed(app, "ResetButton");
    REQUIRE(button != nullptr);
    husky::engine::UIButtonComponent* buttonComponent = button->GetComponent<husky::engine::UIButtonComponent>();
    REQUIRE(buttonComponent != nullptr);
    husky::engine::ClickUIButton(*button, *buttonComponent);
    CHECK(scoreText->text == "スコア: 0");
    CHECK(timeText->text == "残り時間: 01:00");
    CHECK_FALSE(hud->IsFinished());
}

TEST_CASE("Game: プレイヤーのスコアは、跳ね回るものが触れ始めたときだけ増える")
{
    Application app;
    auto* player = app.GetScene().Spawn<husky::game::PlayerActor>();
    auto* bouncer = app.GetScene().Spawn<husky::game::BouncerActor>();
    auto* other = app.GetScene().Spawn<husky::engine::Actor>();
    REQUIRE(player != nullptr);
    REQUIRE(bouncer != nullptr);
    REQUIRE(other != nullptr);
    husky::engine::OverlapEvent event;
    event.begin = true;
    event.otherActor = bouncer->GetHandle();
    player->OnOverlap(event);
    event.begin = false; // 離れたときは数えない
    player->OnOverlap(event);
    event.begin = true;
    event.otherActor = other->GetHandle(); // 跳ね回るもの以外
    player->OnOverlap(event);
    CHECK(player->score == 1);
}
