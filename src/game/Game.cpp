// Game.h と GameActors.h の実装。
#include "game/Game.h"

#include "engine/AssetLoader.h"
#include "engine/CameraComponent.h"
#include "game/CameraFollowComponent.h"
#include "engine/Input.h"
#include "engine/Prefab.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "game/GameActors.h"

#include <algorithm> // std::clamp
#include <cstdint>
#include <cstdio> // std::printf
#include <random> // std::mt19937(乱数)
#include <string>

namespace husky::game
{
    namespace
    {
        // 1つの軸について、位置を進め、範囲(minPosition〜maxPosition)の端に当たったら跳ね返す。
        // position と velocity は参照(&)なので、呼んだ側の変数が書き換わる。
        void MoveAndBounce(float& position, float& velocity, float dt, float minPosition, float maxPosition)
        {
            position += velocity * dt;
            if (position < minPosition)
            {
                position = minPosition;
                velocity = -velocity;
            }
            else if (position > maxPosition)
            {
                position = maxPosition;
                velocity = -velocity;
            }
        }
        // assets の中のパスでプレハブを探して読み、読み終わったアセットを返す。無い・読めないときはログに出して nullptr。
        // 最初の配置は起動のときに1回だけなので、読み終わるまで待つ(WaitForAllLoads)。
        const engine::PrefabAsset* LoadPrefab(engine::Application& app, const char* path)
        {
            const core::Guid* guid = app.GetAssetDatabase().FindGuid(path);
            if (guid == nullptr)
            {
                std::printf("プレハブがアセットの表にありません(%s)\n", path);
                return nullptr;
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            const engine::PrefabAsset* prefab = loader.ResolvePrefab(loader.LoadPrefab(*guid));
            loader.WaitForAllLoads();
            if (prefab == nullptr || !prefab->IsLoaded())
            {
                std::printf("プレハブを読めませんでした(%s)\n", path);
                return nullptr;
            }
            return prefab;
        }

        // プレハブを1つ置き、T の型として返す(プレハブの中のアクターが T でなければ nullptr)。
        // 置いたものの警告(知らないプロパティなど)はログに出す。
        template <typename T>
        T* PlacePrefab(engine::Application& app, const engine::PrefabAsset& prefab)
        {
            engine::Prefab::InstantiateResult placed =
                engine::Prefab::Instantiate(prefab, app.GetScene(), app.GetTypeRegistry());
            for (const std::string& warning : placed.warnings)
            {
                std::printf("プレハブの注意: %s\n", warning.c_str());
            }
            return core::Cast<T>(placed.actor);
        }

        // 跳ね回るアクターを count 個、プレハブ(bouncer.prefab: BouncerActor + 32x32 の白い SpriteComponent)から置く。
        // プレハブにあるのは全員に共通の値だけ。1つずつ違う位置、速度、色は、置いた後に書く
        // (Unity でプレハブを置いてから Inspector で値を変えるのと同じ。PF-4 では、この「違い」だけがシーンに保存される)。
        // 乱数の種(seed)を固定しているので、毎回同じ配置になる(CLAUDE.md: 結果が毎回同じになるように)。
        // 乱数は S-2 までと同じ型(double)で、同じ順番に引く。型や順番が変わると乱数の消費のしかたが変わり、配置が変わってしまうため。
        void SpawnBouncers(engine::Application& app, const engine::PrefabAsset& prefab, int count)
        {
            std::mt19937 random(12345); // 種を固定した乱数生成器
            std::uniform_real_distribution<double> randomX(0.0, WorldWidth - BouncerSize);
            std::uniform_real_distribution<double> randomY(0.0, WorldHeight - BouncerSize);
            std::uniform_real_distribution<double> randomSpeed(-200.0, 200.0);
            std::uniform_real_distribution<float> randomColor(0.4f, 1.0f);

            for (int i = 0; i < count; ++i)
            {
                BouncerActor* bouncer = PlacePrefab<BouncerActor>(app, prefab);
                engine::SpriteComponent* sprite =
                    bouncer != nullptr ? bouncer->GetComponent<engine::SpriteComponent>() : nullptr;
                if (sprite == nullptr) // オブジェクト用のメモリを使い切った、またはプレハブの形が違う
                {
                    std::printf("跳ね回るアクターを置けませんでした(%d 個目)\n", i);
                    return;
                }
                // 位置はスプライトの真ん中(ピボットが真ん中のため)。乱数は左下の角として引き、大きさの半分を足す
                // (乱数の引き方を変えないので、配置は今までと同じ見た目になる)。
                bouncer->transform.position.x = static_cast<float>(randomX(random)) + BouncerSize / 2.0f;
                bouncer->transform.position.y = static_cast<float>(randomY(random)) + BouncerSize / 2.0f;
                bouncer->vx = static_cast<float>(randomSpeed(random));
                bouncer->vy = static_cast<float>(randomSpeed(random));
                sprite->color = {randomColor(random), randomColor(random), randomColor(random), 1.0f};
            }
        }
    }

    void BouncerActor::Tick(double dt)
    {
        const float step = static_cast<float>(dt);
        // 位置は真ん中なので、世界の端から大きさの半分だけ内側で跳ね返る。
        const float half = BouncerSize / 2.0f;
        MoveAndBounce(transform.position.x, vx, step, half, WorldWidth - half);
        MoveAndBounce(transform.position.y, vy, step, half, WorldHeight - half);
    }

    void BouncerActor::OnOverlap(const engine::OverlapEvent& event)
    {
        // 見た目は SpriteComponent の色で変える(無ければ何もしない)。重なりはプレイヤーとだけ起きる(グループの決め事)
        engine::SpriteComponent* sprite = GetComponent<engine::SpriteComponent>();
        if (sprite == nullptr)
        {
            return;
        }
        if (event.begin)
        {
            if (touching++ == 0) // 触れ始め(0 → 1): 元の色を控えてから赤くする
            {
                savedColor = sprite->color;
                sprite->color = TouchedColor;
            }
        }
        else if (touching > 0 && --touching == 0) // 最後の相手から離れた(1 → 0): 元の色に戻す
        {
            sprite->color = savedColor;
        }
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

        // 世界の外へは出られない(位置は真ん中なので、四方の端から大きさの半分だけ内側まで)。
        // std::clamp(値, 下限, 上限): 値を範囲の中に収める(C# の Math.Clamp)。
        const float half = PlayerSize / 2.0f;
        transform.position.x = std::clamp(transform.position.x, half, WorldWidth - half);
        transform.position.y = std::clamp(transform.position.y, half, WorldHeight - half);
    }

    void RegisterGameTypes(core::TypeRegistry& types)
    {
        types.Register<BouncerActor>();
        types.Register<PlayerActor>();
        types.Register<CameraFollowComponent>();
    }

    void SpawnInitialActors(engine::Application& app)
    {
        // どのプレハブを使うかは、ゲームの決め事なのでここ(ゲームのコード)に書く。パスで探し、GUID で読む。
        const engine::PrefabAsset* bouncerPrefab = LoadPrefab(app, "prefabs/bouncer.prefab");
        const engine::PrefabAsset* playerPrefab = LoadPrefab(app, "prefabs/player.prefab");
        engine::Scene& scene = app.GetScene();

        if (bouncerPrefab != nullptr)
        {
            SpawnBouncers(app, *bouncerPrefab, BouncerCount);
        }

        // カメラ: プレイヤーを追いかける(追いかける相手は、プレイヤーを作った後に決める)。
        // カメラは特別な型ではなく、ふつうのアクターにカメラのコンポーネントを付けたもの。
        // プレハブにしないのは、追いかける相手(ほかのアクターへのハンドル)がプレハブの外にいて、プレハブには書けないため。
        engine::Actor* camera = scene.Spawn<engine::Actor>();
        if (camera != nullptr)
        {
            camera->name = "MainCamera";
            camera->AddComponent<engine::CameraComponent>();
        }

        // プレイヤー(player.prefab): PlayerActor + スプライトシート(shapes.spritesheet)の切り出しを貼る SpriteComponent
        // + 4 つの図形を順に切り替えるアニメーション(SpriteAnimatorComponent)。見た目の決め事は、すべてプレハブの中にある。
        PlayerActor* player = playerPrefab != nullptr ? PlacePrefab<PlayerActor>(app, *playerPrefab) : nullptr;
        if (player != nullptr)
        {
            player->transform.position.x = WorldWidth / 2.0f; // 世界の真ん中(位置はスプライトの真ん中)
            player->transform.position.y = WorldHeight / 2.0f;
        }

        if (camera != nullptr && player != nullptr)
        {
            if (game::CameraFollowComponent* follow = camera->AddComponent<game::CameraFollowComponent>())
            {
                follow->target = player->GetHandle();
                // プレイヤーの位置は真ん中なので、ずらさずにそのまま画面の真ん中に映す。
                follow->offset = {0.0f, 0.0f, 0.0f};
            }
            // 最初から追いついた位置に置く(編集中は Tick が止まり、追いかけないため)。
            camera->transform.position.x = player->transform.position.x;
            camera->transform.position.y = player->transform.position.y;
        }
    }
}
