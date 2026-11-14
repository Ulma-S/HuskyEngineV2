// サンプルのゲームの流れ(SC-5): master を開く → タイトル → ゲーム → リザルト → タイトル を、exe の隣の assets でヘッドレスに回す。
#include "engine/Application.h"
#include "engine/Scene.h"
#include "engine/UIComponents.h"
#include "game/Game.h"
#include "game/GameActors.h"
#include "game/GameFlow.h"
#include "game/HudComponent.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::World;
using husky::game::GameFlowButtonComponent;
using husky::game::GameFlowComponent;
using husky::game::GameFlowState;

namespace
{
    Actor* FindActor(World& world, const std::string& actorName)
    {
        Actor* found = nullptr;
        world.ForEachActor([&](Actor& actor) {
            if (found == nullptr && actor.name == actorName)
            {
                found = &actor;
            }
        });
        return found;
    }

    // 16 ms ずつ進める
    void Step(Application& app, std::uint64_t& now, int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            now += 16'000'000;
            app.StepFrame(now, {});
        }
    }
}

TEST_CASE("GameFlow: master を開き、タイトル → ゲーム → リザルト → タイトル と子のシーンを読み替える(SC-5)")
{
    Application app;
    husky::game::RegisterGameTypes(app.GetTypeRegistry());
    REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    const husky::core::Guid* list = app.GetAssetDatabase().FindGuid("settings/scenes.sdata");
    REQUIRE(list != nullptr);
    REQUIRE(app.LoadSceneList(*list));
    CHECK(app.GetFirstSceneName() == "master");
    REQUIRE(app.OpenRootScene("master"));
    World& world = app.GetWorld();

    // 開いた直後: system(常駐)と title が読まれている
    REQUIRE(world.FindScene("system") != nullptr);
    REQUIRE(world.FindScene("title") != nullptr);
    CHECK(world.FindScene("game") == nullptr);
    GameFlowComponent* flow = husky::game::FindGameFlow(world);
    REQUIRE(flow != nullptr);
    CHECK(flow->state == GameFlowState::Title);
    Actor* music = FindActor(world, "Music");
    REQUIRE(music != nullptr);
    CHECK(music->GetScene() == world.FindScene("system"));

    // Start ボタン(を押したときに呼ばれるもの)→ このフレームの終わりに title を下ろして game を読む
    std::uint64_t now = 0;
    Actor* start = FindActor(world, "StartButton");
    REQUIRE(start != nullptr);
    REQUIRE(start->GetComponent<GameFlowButtonComponent>() != nullptr);
    start->GetComponent<GameFlowButtonComponent>()->Invoke();
    Step(app, now);
    CHECK(flow->state == GameFlowState::Playing);
    CHECK(world.FindScene("title") == nullptr);
    husky::engine::Scene* game = world.FindScene("game");
    REQUIRE(game != nullptr);

    // 次のフレームで、置く係がゲームの配置と HUD を game のシーンに置く
    Step(app, now, 2);
    Actor* player = nullptr;
    world.ForEachActor([&](Actor& actor) {
        if (player == nullptr && actor.IsA<husky::game::PlayerActor>())
        {
            player = &actor;
        }
    });
    REQUIRE(player != nullptr);
    CHECK(player->GetScene() == game);
    husky::game::HudComponent* hud = world.FindComponent<husky::game::HudComponent>();
    REQUIRE(hud != nullptr);
    CHECK(hud->GetOwner()->GetScene() == game);

    // 時間切れ(残り時間を短くする)→ 2 秒後に HUD がスコアを渡してリザルトへ
    hud->timeLimit = 0.05f;
    hud->ResetGame();
    Step(app, now, 140); // 約 2.2 秒
    CHECK(flow->state == GameFlowState::Result);
    CHECK(world.FindScene("game") == nullptr);
    REQUIRE(world.FindScene("result") != nullptr);
    CHECK(world.FindComponent<husky::game::HudComponent>() == nullptr); // ゲームのものは消えた
    Step(app, now);
    Actor* scoreText = FindActor(world, "ScoreText");
    REQUIRE(scoreText != nullptr);
    CHECK(scoreText->GetComponent<husky::engine::UITextComponent>()->text ==
          "スコア: " + std::to_string(flow->GetLastScore()));

    // 「タイトルへ」→ タイトルに戻る。BGM はずっと同じアクター(常駐)
    Actor* back = FindActor(world, "BackButton");
    REQUIRE(back != nullptr);
    back->GetComponent<GameFlowButtonComponent>()->Invoke();
    Step(app, now);
    CHECK(flow->state == GameFlowState::Title);
    CHECK(world.FindScene("result") == nullptr);
    CHECK(world.FindScene("title") != nullptr);
    CHECK(FindActor(world, "Music") == music);

    // もう一周できる(同じシーンを読み直せる)
    FindActor(world, "StartButton")->GetComponent<GameFlowButtonComponent>()->Invoke();
    Step(app, now, 2);
    CHECK(world.FindScene("game") != nullptr);
}
