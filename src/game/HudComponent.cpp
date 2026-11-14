// HudComponent.h の実装。
#include "game/HudComponent.h"

#include "engine/Actor.h"
#include "engine/Scene.h"
#include "game/GameActors.h"

#include <cmath>   // std::ceil
#include <cstdio>  // std::snprintf

namespace husky::game
{
    int HudComponent::ReadPlayerScore()
    {
        engine::Actor* owner = GetOwner();
        engine::Scene* scene = owner != nullptr ? owner->GetScene() : nullptr;
        core::ObjectRegistry* registry = GetOwnerRegistry();
        if (scene == nullptr || registry == nullptr)
        {
            return 0;
        }
        const PlayerActor* player = registry->Resolve<PlayerActor>(m_player);
        if (player == nullptr || player->IsPendingDestroy())
        {
            // 見失ったら探し直す(シーンの一覧の最初のプレイヤー)
            player = nullptr;
            scene->ForEachActor([&](engine::Actor& actor) {
                if (player == nullptr && actor.IsA<PlayerActor>())
                {
                    player = static_cast<const PlayerActor*>(&actor);
                }
            });
            m_player = player != nullptr ? player->GetHandle() : core::ObjectHandle{};
        }
        return player != nullptr ? player->score : 0;
    }

    void HudComponent::Tick(double dt)
    {
        if (!m_started)
        {
            m_started = true;
            m_remaining = timeLimit;
            m_scoreBase = ReadPlayerScore();
        }
        if (m_remaining > 0.0f)
        {
            m_remaining -= static_cast<float>(dt);
            if (m_remaining < 0.0f)
            {
                m_remaining = 0.0f;
            }
            m_score = ReadPlayerScore() - m_scoreBase; // 時間切れの後は数えない(最後の値のまま)
        }
        UpdateTexts();
    }

    void HudComponent::ResetGame()
    {
        m_started = true;
        m_remaining = timeLimit;
        m_scoreBase = ReadPlayerScore();
        m_score = 0;
        UpdateTexts();
    }

    void HudComponent::UpdateTexts()
    {
        core::ObjectRegistry* registry = GetOwnerRegistry();
        if (registry == nullptr)
        {
            return;
        }
        char buffer[64];
        if (m_score != m_shownScore)
        {
            if (engine::UITextComponent* text = scoreText.Resolve(*registry))
            {
                std::snprintf(buffer, sizeof(buffer), "スコア: %d", m_score);
                text->text = buffer;
            }
            m_shownScore = m_score;
        }
        // 残りの秒は切り上げ(59.2 秒は「00:60」ではなく「01:00」と出したいので、切り上げてから分と秒に分ける)
        const int seconds = static_cast<int>(std::ceil(m_remaining));
        if (seconds != m_shownSeconds)
        {
            if (engine::UITextComponent* text = timeText.Resolve(*registry))
            {
                if (seconds <= 0)
                {
                    text->text = "おしまい!";
                }
                else
                {
                    std::snprintf(buffer, sizeof(buffer), "残り時間: %02d:%02d", seconds / 60, seconds % 60);
                    text->text = buffer;
                }
            }
            m_shownSeconds = seconds;
        }
    }
}
