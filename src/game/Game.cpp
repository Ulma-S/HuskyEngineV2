// Game.h と GameActors.h の実装。
#include "game/Game.h"

#include "engine/CameraComponent.h"
#include "game/CameraFollowComponent.h"
#include "engine/Input.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "game/GameActors.h"

#include <algorithm> // std::clamp
#include <cstdint>
#include <cstdio> // std::printf
#include <random> // std::mt19937(乱数)

namespace husky::game
{
    namespace
    {
        // 1つの軸について、位置を進め、画面の端に当たったら跳ね返す。
        // position と velocity は参照(&)なので、呼んだ側の変数が書き換わる。
        void MoveAndBounce(float& position, float& velocity, float dt, float maxPosition)
        {
            position += velocity * dt;
            if (position < 0.0f)
            {
                position = 0.0f;
                velocity = -velocity;
            }
            else if (position > maxPosition)
            {
                position = maxPosition;
                velocity = -velocity;
            }
        }

        // 跳ね回るアクターを count 個、シーンに置く。
        // 乱数の種(seed)を固定しているので、毎回同じ配置になる(CLAUDE.md: 結果が毎回同じになるように)。
        // 乱数は S-2 までと同じ型(double)で引く。型が変わると乱数の消費のしかたが変わり、配置が変わってしまうため。
        void SpawnBouncers(engine::Scene& scene, int count)
        {
            std::mt19937 random(12345); // 種を固定した乱数生成器
            std::uniform_real_distribution<double> randomX(0.0, WorldWidth - BouncerSize);
            std::uniform_real_distribution<double> randomY(0.0, WorldHeight - BouncerSize);
            std::uniform_real_distribution<double> randomSpeed(-200.0, 200.0);
            std::uniform_real_distribution<float> randomColor(0.4f, 1.0f);

            for (int i = 0; i < count; ++i)
            {
                BouncerActor* bouncer = scene.Spawn<BouncerActor>();
                engine::SpriteComponent* sprite =
                    bouncer != nullptr ? bouncer->AddComponent<engine::SpriteComponent>() : nullptr;
                if (sprite == nullptr) // オブジェクト用のメモリを使い切った
                {
                    std::printf("跳ね回るアクターを作れませんでした(%d 個目)\n", i);
                    return;
                }
                bouncer->transform.position.x = static_cast<float>(randomX(random));
                bouncer->transform.position.y = static_cast<float>(randomY(random));
                bouncer->vx = static_cast<float>(randomSpeed(random));
                bouncer->vy = static_cast<float>(randomSpeed(random));
                sprite->width = BouncerSize;
                sprite->height = BouncerSize;
                sprite->color = {randomColor(random), randomColor(random), randomColor(random), 1.0f};
            }
        }
    }

    void BouncerActor::Tick(double dt)
    {
        const float step = static_cast<float>(dt);
        MoveAndBounce(transform.position.x, vx, step, WorldWidth - BouncerSize);
        MoveAndBounce(transform.position.y, vy, step, WorldHeight - BouncerSize);
    }

    void PlayerActor::Tick(double dt)
    {
        const engine::Scene* scene = GetScene();
        const engine::Input* input = scene != nullptr ? scene->GetInput() : nullptr;
        if (input == nullptr)
        {
            return;
        }
        // 押されている方向キーから、動く向きを決める(-1, 0, +1)。
        float moveX = 0.0f;
        float moveY = 0.0f;
        if (input->IsDown(engine::Key::Left) || input->IsDown(engine::Key::A))  { moveX -= 1.0f; }
        if (input->IsDown(engine::Key::Right) || input->IsDown(engine::Key::D)) { moveX += 1.0f; }
        if (input->IsDown(engine::Key::Up) || input->IsDown(engine::Key::W))    { moveY += 1.0f; } // 上は +y
        if (input->IsDown(engine::Key::Down) || input->IsDown(engine::Key::S))  { moveY -= 1.0f; }

        // 速さ × dt = このフレームで進む距離。フレームレートが違っても、1秒で進む距離は同じになる。
        const float step = moveSpeed * static_cast<float>(dt);
        transform.position.x += moveX * step;
        transform.position.y += moveY * step;

        // 世界の外へは出られない(位置は左下の角なので、右と上の端は大きさの分だけ手前)。
        // std::clamp(値, 下限, 上限): 値を範囲の中に収める(C# の Math.Clamp)。
        transform.position.x = std::clamp(transform.position.x, 0.0f, WorldWidth - PlayerSize);
        transform.position.y = std::clamp(transform.position.y, 0.0f, WorldHeight - PlayerSize);
    }

    void RegisterGameTypes(core::TypeRegistry& types)
    {
        types.Register<BouncerActor>();
        types.Register<PlayerActor>();
        types.Register<CameraFollowComponent>();
    }

    void SpawnInitialActors(engine::Scene& scene, const engine::AssetDatabase& assets)
    {
        // shapes.spritesheet の「diamond」の切り出しと、「cycle」のアニメーション(4 つの図形を 0.25 秒ずつ順に)の ID
        // (ファイルに書いてある。名前を変えても、この ID は変わらない)。
        constexpr std::int32_t DiamondSpriteId = 1917386538;
        constexpr std::int32_t CycleAnimationId = 1811624963;

        SpawnBouncers(scene, BouncerCount);

        // カメラ: プレイヤーを追いかける(追いかける相手は、プレイヤーを作った後に決める)。
        // カメラは特別な型ではなく、ふつうのアクターにカメラのコンポーネントを付けたもの。
        engine::Actor* camera = scene.Spawn<engine::Actor>();
        if (camera != nullptr)
        {
            camera->name = "MainCamera";
            camera->AddComponent<engine::CameraComponent>();
        }

        PlayerActor* player = scene.Spawn<PlayerActor>();
        if (player != nullptr)
        {
            if (engine::SpriteComponent* sprite = player->AddComponent<engine::SpriteComponent>())
            {
                sprite->width = PlayerSize;
                sprite->height = PlayerSize; // 色は既定の白(テクスチャのまま)
                // アセットは GUID で指す。読むのは、描くとき(DrawSprites)。
                if (const core::Guid* shapes = assets.FindGuid("sprites/shapes.spritesheet"))
                {
                    sprite->sheet.guid = *shapes;
                    sprite->sprite = DiamondSpriteId; // スプライトシートが読めるまでの間に描くコマ(アニメーターが書き換える)
                }
            }
            // 見た目(SpriteComponent)とは別の部品で、切り出しを時間で切り替える(Unity の SpriteRenderer と Animator)。
            if (engine::SpriteAnimatorComponent* animator = player->AddComponent<engine::SpriteAnimatorComponent>())
            {
                animator->Play(CycleAnimationId);
            }
            player->transform.position.x = (WorldWidth - PlayerSize) / 2.0f; // 世界の真ん中
            player->transform.position.y = (WorldHeight - PlayerSize) / 2.0f;
        }

        if (camera != nullptr && player != nullptr)
        {
            if (game::CameraFollowComponent* follow = camera->AddComponent<game::CameraFollowComponent>())
            {
                follow->target = player->GetHandle();
                // プレイヤーの位置は左下の角なので、大きさの半分だけずらして、プレイヤーの真ん中を画面の真ん中に映す。
                follow->offset = {PlayerSize / 2.0f, PlayerSize / 2.0f, 0.0f};
            }
            // 最初から追いついた位置に置く(編集中は Tick が止まり、追いかけないため)。
            camera->transform.position.x = player->transform.position.x + PlayerSize / 2.0f;
            camera->transform.position.y = player->transform.position.y + PlayerSize / 2.0f;
        }
    }
}
