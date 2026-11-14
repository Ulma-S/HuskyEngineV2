// GameFlow.h の実装。
#include "game/GameFlow.h"

#include "engine/Actor.h"
#include "engine/World.h"
#include "game/Game.h"
#include "game/GameLog.h"

#include <cstdio> // std::snprintf
#include <vector>

namespace husky::game
{
    GameFlowComponent* FindGameFlow(engine::World& world)
    {
        return world.FindComponent<GameFlowComponent>();
    }

    void GameFlowComponent::Switch(const std::string& from, const std::string& to)
    {
        engine::Actor* owner = GetOwner();
        engine::World* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (world == nullptr)
        {
            return;
        }
        // 頼むだけ。実際に下ろす・読むのは、このフレームの物理の後(Application::ProcessSceneRequests)。
        world->UnloadScene(from);
        world->LoadScene(to); // 親を省くとルートシーン(master)の下
    }

    void GameFlowComponent::StartGame()
    {
        if (state != GameFlowState::Title)
        {
            return; // 2回押しても、2回読まない
        }
        state = GameFlowState::Playing;
        Switch(titleScene, gameScene);
    }

    void GameFlowComponent::FinishGame(int score)
    {
        if (state != GameFlowState::Playing)
        {
            return;
        }
        m_lastScore = score;
        state = GameFlowState::Result;
        Switch(gameScene, resultScene);
    }

    void GameFlowComponent::BackToTitle()
    {
        if (state != GameFlowState::Result)
        {
            return;
        }
        state = GameFlowState::Title;
        Switch(resultScene, titleScene);
    }

    void GameFlowButtonComponent::Invoke()
    {
        engine::Actor* owner = GetOwner();
        engine::World* world = owner != nullptr ? owner->GetWorld() : nullptr;
        GameFlowComponent* flow = world != nullptr ? FindGameFlow(*world) : nullptr;
        if (flow == nullptr)
        {
            HUSKY_LOG(GameLog::GameFlowMissing, owner != nullptr ? owner->name : std::string());
            return;
        }
        switch (action)
        {
        case GameFlowAction::StartGame:
            flow->StartGame();
            break;
        case GameFlowAction::BackToTitle:
            flow->BackToTitle();
            break;
        }
    }

    void ResultComponent::Tick(double)
    {
        if (m_written)
        {
            return;
        }
        m_written = true;
        engine::Actor* owner = GetOwner();
        engine::World* world = owner != nullptr ? owner->GetWorld() : nullptr;
        core::ObjectRegistry* registry = GetOwnerRegistry();
        const GameFlowComponent* flow = world != nullptr ? FindGameFlow(*world) : nullptr;
        engine::UITextComponent* text = registry != nullptr ? scoreText.Resolve(*registry) : nullptr;
        if (text != nullptr)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "スコア: %d", flow != nullptr ? flow->GetLastScore() : 0);
            text->text = buffer;
        }
    }

    void GameSpawnerComponent::Tick(double)
    {
        if (m_spawned)
        {
            return;
        }
        m_spawned = true;
        engine::Actor* owner = GetOwner();
        engine::World* world = owner != nullptr ? owner->GetWorld() : nullptr;
        engine::Scene* scene = owner != nullptr ? owner->GetScene() : nullptr;
        if (world == nullptr || scene == nullptr || world->GetAssetLoader() == nullptr)
        {
            return;
        }
        // 置く関数はルートシーンに置くので、置いた後に、増えた根のアクター(子孫も一緒に)を自分のシーンへ移す。
        const std::size_t before = world->actors.size();
        SpawnGameplay(*world, false); // BGM は常駐の system のシーンが持つ
        SpawnHud(*world);
        std::vector<engine::Actor*> placed;
        for (std::size_t i = before; i < world->actors.size(); ++i)
        {
            engine::Actor* actor = GetOwnerRegistry()->Resolve<engine::Actor>(world->actors[i]);
            if (actor != nullptr && actor->GetParent() == nullptr)
            {
                placed.push_back(actor);
            }
        }
        for (engine::Actor* actor : placed)
        {
            world->MoveToScene(*actor, *scene);
        }
    }
}
