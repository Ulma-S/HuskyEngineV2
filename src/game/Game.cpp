// Game.h と GameActors.h の実装。
#include "game/Game.h"

#include "engine/CameraComponent.h"
#include "engine/Input.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "game/GameActors.h"

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
            std::uniform_real_distribution<double> randomX(0.0, ScreenWidth - BouncerSize);
            std::uniform_real_distribution<double> randomY(0.0, ScreenHeight - BouncerSize);
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
        MoveAndBounce(transform.position.x, vx, step, ScreenWidth - BouncerSize);
        MoveAndBounce(transform.position.y, vy, step, ScreenHeight - BouncerSize);
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
    }

    void RegisterGameTypes(core::TypeRegistry& types)
    {
        types.Register<BouncerActor>();
        types.Register<PlayerActor>();
    }

    void SpawnInitialActors(engine::Scene& scene, const engine::AssetDatabase& assets)
    {
        // shapes.spritesheet の「diamond」の切り出しと、「cycle」のアニメーション(4 つの図形を 0.25 秒ずつ順に)の ID
        // (ファイルに書いてある。名前を変えても、この ID は変わらない)。
        constexpr std::int32_t DiamondSpriteId = 1917386538;
        constexpr std::int32_t CycleAnimationId = 1811624963;

        SpawnBouncers(scene, BouncerCount);

        // カメラ: 画面の真ん中を映す(zoom 1 なら、カメラが無いときと同じ見え方)。
        // カメラは特別な型ではなく、ふつうのアクターにカメラのコンポーネントを付けたもの。
        if (engine::Actor* camera = scene.Spawn<engine::Actor>())
        {
            camera->name = "MainCamera";
            camera->AddComponent<engine::CameraComponent>();
            camera->transform.position.x = ScreenWidth / 2.0f;
            camera->transform.position.y = ScreenHeight / 2.0f;
        }

        if (PlayerActor* player = scene.Spawn<PlayerActor>())
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
            player->transform.position.x = (ScreenWidth - PlayerSize) / 2.0f; // 画面の中央
            player->transform.position.y = (ScreenHeight - PlayerSize) / 2.0f;
        }
    }
}
