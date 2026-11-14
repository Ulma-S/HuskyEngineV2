// Game.h と GameActors.h の実装。
#include "game/Game.h"
#include "game/GameLog.h"

#include "engine/AssetLoader.h"
#include "engine/AudioSourceComponent.h"
#include "engine/CameraComponent.h"
#include "engine/Collider2D.h"
#include "engine/Rigidbody2D.h"
#include "game/CameraFollowComponent.h"
#include "game/HudComponent.h"
#include "engine/Input.h"
#include "engine/Prefab.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "engine/UIActor.h"
#include "game/GameActors.h"

#include <algorithm> // std::clamp
#include <cmath>     // std::sqrt
#include <cstdint>

#include <random> // std::mt19937(乱数)
#include <string>

namespace husky::game
{
    // ゲームの型の型情報の中身(HR-1。Object.h の HUSKY_OBJECT の説明)。ゲームに型を足したら、ここにも足す。
    HUSKY_OBJECT_DEFINE(BouncerSettings)
    HUSKY_OBJECT_DEFINE(CameraFollowComponent)
    HUSKY_OBJECT_DEFINE(BouncerActor)
    HUSKY_OBJECT_DEFINE(PlayerActor)
    HUSKY_OBJECT_DEFINE(HudComponent)

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
                HUSKY_LOG(GameLog::PrefabNotInDatabase, path);
                return nullptr;
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            const engine::PrefabAsset* prefab = loader.Resolve<engine::PrefabAsset>(loader.Load<engine::PrefabAsset>(*guid));
            loader.WaitForAllLoads();
            if (prefab == nullptr || !prefab->IsLoaded())
            {
                HUSKY_LOG(GameLog::PrefabLoadFailed, path);
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
                engine::Prefab::Instantiate(prefab, app.GetWorld(), app.GetTypeRegistry());
            for (const std::string& warning : placed.warnings)
            {
                HUSKY_LOG(GameLog::PrefabWarning, warning);
            }
            return core::Cast<T>(placed.actor);
        }

        // 設定のデータ(.sdata)を assets の中のパスで探して読み、読み終わった中身を返す。無い・読めないときはログに出して nullptr。
        // reference は呼んだ側が持つ(持っている間は、データが GC に回収されない)。
        const BouncerSettings* LoadSettings(engine::Application& app, const char* path,
                                            core::AssetRef<BouncerSettings>& reference)
        {
            const core::Guid* guid = app.GetAssetDatabase().FindGuid(path);
            if (guid == nullptr)
            {
                HUSKY_LOG(GameLog::SettingsNotInDatabase, path);
                return nullptr;
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            reference.guid = *guid;
            loader.Resolve(reference); // 読み始める
            loader.WaitForAllLoads();
            const BouncerSettings* settings = loader.Resolve(reference);
            if (settings == nullptr)
            {
                HUSKY_LOG(GameLog::SettingsLoadFailed, path);
            }
            return settings;
        }

        // 跳ね回るアクターを count 個、プレハブ(bouncer.prefab: BouncerActor + 32x32 の白い SpriteComponent)から置く。
        // プレハブにあるのは全員に共通の値だけ。1つずつ違う位置、速度、色は、置いた後に書く
        // (Unity でプレハブを置いてから Inspector で値を変えるのと同じ。PF-4 では、この「違い」だけがシーンに保存される)。
        // 乱数の種(seed)を固定しているので、毎回同じ配置になる(CLAUDE.md: 結果が毎回同じになるように)。
        // 乱数は S-2 までと同じ型(double)で、同じ順番に引く。型や順番が変わると乱数の消費のしかたが変わり、配置が変わってしまうため。
        // 速さの上限 maxSpeed は設定のデータ(BouncerSettings)から。既定の 200 なら、配置も速度も今までと同じになる。
        void SpawnBouncers(engine::Application& app, const engine::PrefabAsset& prefab, int count, float maxSpeed)
        {
            std::mt19937 random(12345); // 種を固定した乱数生成器
            std::uniform_real_distribution<double> randomX(0.0, WorldWidth - BouncerSize);
            std::uniform_real_distribution<double> randomY(0.0, WorldHeight - BouncerSize);
            std::uniform_real_distribution<double> randomSpeed(-static_cast<double>(maxSpeed), static_cast<double>(maxSpeed));
            std::uniform_real_distribution<float> randomColor(0.4f, 1.0f);

            for (int i = 0; i < count; ++i)
            {
                BouncerActor* bouncer = PlacePrefab<BouncerActor>(app, prefab);
                engine::SpriteComponent* sprite =
                    bouncer != nullptr ? bouncer->GetComponent<engine::SpriteComponent>() : nullptr;
                if (sprite == nullptr) // オブジェクト用のメモリを使い切った、またはプレハブの形が違う
                {
                    HUSKY_LOG(GameLog::BouncerSpawnFailed, i);
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

        // 木箱を、プレイヤーの最初の場所(世界の真ん中)の右に 4 個 × 2 段で置く(PHY-5)。
        // 1 つずつ違うのは位置だけ(ほかの値はプレハブのもの)。
        void SpawnCrates(engine::Application& app, const engine::PrefabAsset& prefab)
        {
            for (int i = 0; i < CrateCount; ++i)
            {
                engine::Actor* crate = PlacePrefab<engine::Actor>(app, prefab);
                if (crate == nullptr)
                {
                    return;
                }
                crate->transform.position.x = WorldWidth / 2.0f + 200.0f + static_cast<float>(i % 4) * 80.0f;
                crate->transform.position.y = WorldHeight / 2.0f + (i < 4 ? 40.0f : -40.0f);
            }
        }

        // 世界の端の 4 枚の壁(PHY-5)。見た目は無く、動かない体(Static)の四角のコライダーだけ。
        // プレイヤーは世界の外へ出ない(PlayerActor::Tick)が、押された木箱はここで止まる。壁は木箱とだけ当たる。
        void SpawnWalls(engine::World& scene)
        {
            constexpr float thickness = 64.0f;
            struct WallPlace
            {
                const char* name;
                float x, y, width, height;
            };
            // 世界の外側に、端の線に接するように置く(四隅もふさぐよう、縦の壁は厚みの分だけ長くする)
            const WallPlace places[] = {
                {"WallLeft", -thickness / 2.0f, WorldHeight / 2.0f, thickness, WorldHeight + thickness * 2.0f},
                {"WallRight", WorldWidth + thickness / 2.0f, WorldHeight / 2.0f, thickness, WorldHeight + thickness * 2.0f},
                {"WallBottom", WorldWidth / 2.0f, -thickness / 2.0f, WorldWidth, thickness},
                {"WallTop", WorldWidth / 2.0f, WorldHeight + thickness / 2.0f, WorldWidth, thickness},
            };
            for (const WallPlace& place : places)
            {
                engine::Actor* wall = scene.Spawn<engine::Actor>();
                if (wall == nullptr)
                {
                    return;
                }
                wall->name = place.name;
                wall->transform.position = {place.x, place.y, 0.0f};
                if (engine::Rigidbody2D* body = wall->AddComponent<engine::Rigidbody2D>())
                {
                    body->bodyType = engine::BodyType2D::Static;
                }
                if (engine::BoxCollider2D* box = wall->AddComponent<engine::BoxCollider2D>())
                {
                    box->size = {place.width, place.height};
                    box->layer = WallLayer;
                    box->collisionMask = 1 << CrateLayer;
                }
            }
        }
    }

    void BouncerActor::Tick(double dt)
    {
        const float step = static_cast<float>(dt);
        // 設定のデータを引いておく(初めてなら読み始める)。触れた瞬間に初めて引くと、読み終わるまでの数フレームは
        // 既定の色になってしまうため。引いた後は AssetRef が強い参照で持つので、データは回収されない。
        // ふつうは表を1回引くだけで軽い(1000 体でも問題ない)。
        if (engine::AssetLoader* loader = GetWorld() != nullptr ? GetWorld()->GetAssetLoader() : nullptr)
        {
            loader->Resolve(settings);
            loader->Resolve(touchSound); // 音も同じ理由で先に読んでおく(PlaySoundOnce は読み終わっていなければ鳴らさない)
        }
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
            if (touching++ == 0) // 触れ始め(0 → 1): 元の色を控えてから、設定のデータの色にする
            {
                savedColor = sprite->color;
                if (GetWorld() != nullptr)
                {
                    // 触れ始めの音。たくさん同時に触れたときは、ミキサーが古い効果音から止めるので、鳴りすぎない。
                    engine::PlaySoundOnce(*GetWorld(), touchSound, 0.5f);
                }
                sprite->color = TouchedColor; // データが読めないときの既定
                engine::AssetLoader* loader = GetWorld() != nullptr ? GetWorld()->GetAssetLoader() : nullptr;
                if (const BouncerSettings* data = loader != nullptr ? loader->Resolve(settings) : nullptr)
                {
                    sprite->color = data->touchedColor;
                }
            }
        }
        else if (touching > 0 && --touching == 0) // 最後の相手から離れた(1 → 0): 元の色に戻す
        {
            sprite->color = savedColor;
        }
    }

    void PlayerActor::OnOverlap(const engine::OverlapEvent& event)
    {
        // 触れ始め(begin)で、相手が跳ね回るものなら 1 点(木箱は物理で押し合うので、ここには来ない)
        core::ObjectRegistry* registry = GetOwnerRegistry();
        if (event.begin && registry != nullptr && registry->Resolve<BouncerActor>(event.otherActor) != nullptr)
        {
            ++score;
        }
    }

    void PlayerActor::Tick(double dt)
    {
        engine::World* scene = GetWorld();
        const engine::Input* input = scene != nullptr ? scene->GetInput() : nullptr;
        if (input == nullptr)
        {
            return;
        }
        // 世界の外へは出られない(位置は真ん中なので、四方の端から大きさの半分だけ内側まで)。
        const float half = PlayerSize / 2.0f;

        // 左ボタンを押した瞬間に、マウスの下のワールドの位置を行き先にする(UI-1)。
        // マウスの位置は画面のピクセル(下が +y)なので、カメラを通してワールド(上が +y)へ直す。
        if (input->WasMousePressed(engine::MouseButton::Left))
        {
            core::Vector3 world;
            if (engine::FindMouseWorldPosition(*scene, world))
            {
                // 行き先も世界の中に収める(外を押されても、端で止まれるように)
                m_moveTarget = core::Vector3{std::clamp(world.x, half, WorldWidth - half),
                                             std::clamp(world.y, half, WorldHeight - half), 0.0f};
                m_hasMoveTarget = true;
            }
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
        if (moveX != 0.0f || moveY != 0.0f)
        {
            m_hasMoveTarget = false; // キーで動かしたら、クリックの行き先はやめる
            transform.position.x += moveX * step;
            transform.position.y += moveY * step;
        }
        else if (m_hasMoveTarget)
        {
            // 行き先へまっすぐ進む。このフレームで着ける距離なら、ぴったり行き先に置いてやめる(行き過ぎて揺れないように)。
            const float toX = m_moveTarget.x - transform.position.x;
            const float toY = m_moveTarget.y - transform.position.y;
            const float distance = std::sqrt(toX * toX + toY * toY);
            if (distance <= step)
            {
                transform.position.x = m_moveTarget.x;
                transform.position.y = m_moveTarget.y;
                m_hasMoveTarget = false;
            }
            else
            {
                transform.position.x += toX / distance * step;
                transform.position.y += toY / distance * step;
            }
        }

        // std::clamp(値, 下限, 上限): 値を範囲の中に収める(C# の Math.Clamp)。
        transform.position.x = std::clamp(transform.position.x, half, WorldWidth - half);
        transform.position.y = std::clamp(transform.position.y, half, WorldHeight - half);
    }

    void RegisterGameTypes(core::TypeRegistry& types)
    {
        types.Register<BouncerActor>();
        types.Register<PlayerActor>();
        types.Register<CameraFollowComponent>();
        types.Register<HudComponent>(); // スコアと残り時間の表示(UI-7)
        types.Register<BouncerSettings>(); // .sdata の "type" から作れるように(ScriptableData も同じ登録簿)
    }

    void SpawnInitialActors(engine::Application& app)
    {
        // 跳ね回るものの数・速さ・プレハブは、設定のデータ(data/bouncers.sdata)から読む(SD-4)。
        // コードを直さず、アセットの窓や data.set で調整できる。読めなければ、コードの既定の値を使う。
        core::AssetRef<BouncerSettings> settingsReference;
        const BouncerSettings* settings = LoadSettings(app, "data/bouncers.sdata", settingsReference);

        // どのプレハブを使うかは、ゲームの決め事なのでここ(ゲームのコード)に書く。パスで探し、GUID で読む。
        // 跳ね回るものは、設定のデータがプレハブを指していればそれを使う。
        const engine::PrefabAsset* bouncerPrefab = nullptr;
        if (settings != nullptr && !settings->prefab.IsNull())
        {
            core::AssetRef<engine::PrefabAsset> prefabReference = settings->prefab; // データは読むだけなので、参照は写して使う
            engine::AssetLoader& loader = app.GetAssetLoader();
            loader.Resolve(prefabReference);
            loader.WaitForAllLoads();
            bouncerPrefab = loader.Resolve(prefabReference);
            if (bouncerPrefab != nullptr && !bouncerPrefab->IsLoaded())
            {
                bouncerPrefab = nullptr;
            }
        }
        if (bouncerPrefab == nullptr)
        {
            bouncerPrefab = LoadPrefab(app, "prefabs/bouncer.prefab");
        }
        const engine::PrefabAsset* playerPrefab = LoadPrefab(app, "prefabs/player.prefab");
        engine::World& scene = app.GetWorld();

        if (bouncerPrefab != nullptr)
        {
            SpawnBouncers(app, *bouncerPrefab, settings != nullptr ? settings->count : BouncerCount,
                          settings != nullptr ? settings->maxSpeed : 200.0f);
        }

        // 押せる木箱(PHY-5。crate.prefab: スプライト + 四角のコライダー + Dynamic の Rigidbody2D)と、世界の端の壁。
        // 描く順は一覧の順なので、プレイヤーより先に置く(木箱の上にプレイヤーが描かれる)。
        if (const engine::PrefabAsset* cratePrefab = LoadPrefab(app, "prefabs/crate.prefab"))
        {
            SpawnCrates(app, *cratePrefab);
        }
        SpawnWalls(scene);

        // カメラ: プレイヤーを追いかける(追いかける相手は、プレイヤーを作った後に決める)。
        // カメラは特別な型ではなく、ふつうのアクターにカメラのコンポーネントを付けたもの。
        // プレハブにしないのは、追いかける相手(ほかのアクターへのハンドル)がプレハブの外にいて、プレハブには書けないため。
        engine::Actor* camera = scene.Spawn<engine::Actor>();
        if (camera != nullptr)
        {
            camera->name = "MainCamera";
            camera->AddComponent<engine::CameraComponent>();
        }

        // BGM(AUD-4): 音を鳴らすコンポーネントだけを持つアクター。再生を始めると鳴り始め、ループする。
        // 音のファイルが無ければ(テストのアセットなど)置かない。
        const core::Guid* bgmGuid = app.GetAssetDatabase().FindGuid("sounds/bgm.wav");
        engine::Actor* music = bgmGuid != nullptr ? scene.Spawn<engine::Actor>() : nullptr;
        if (music != nullptr)
        {
            music->name = "Music";
            if (engine::AudioSourceComponent* source = music->AddComponent<engine::AudioSourceComponent>())
            {
                source->sound.guid = *bgmGuid;
                source->volume = 0.4f;
                source->loop = true;
                source->playOnStart = true;
                source->bus = audio::Bus::Music;
            }
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

    void SpawnHudIfMissing(engine::Application& app)
    {
        bool hasCanvas = false;
        app.GetWorld().ForEachActor([&](engine::Actor& actor) {
            if (actor.GetComponent<engine::CanvasComponent>() != nullptr)
            {
                hasCanvas = true;
            }
        });
        if (hasCanvas)
        {
            return;
        }
        if (const engine::PrefabAsset* hudPrefab = LoadPrefab(app, "prefabs/hud.prefab"))
        {
            PlacePrefab<engine::Actor>(app, *hudPrefab);
        }
    }

    void FillGameModule(engine::GameModule& module)
    {
        module.version = engine::GameModuleVersion;
        module.registerTypes = &RegisterGameTypes;
        module.spawnInitialActors = &SpawnInitialActors;
        module.onSceneReady = &SpawnHudIfMissing;
    }
}
