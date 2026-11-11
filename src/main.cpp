// HuskyEngineV2 のエントリポイント(プログラムが最初に実行する関数)。
// 動作スレッド: メインスレッド(= ゲームスレッド)。
//
// ウィンドウを1枚開き、閉じるボタンか Escape が押されるまでゲームループを回す。
// 画面は毎フレーム1色で塗りつぶし、Space を押すと色が切り替わる。
// 跳ね回るスプライトを 1000 個と、矢印キー(または WASD)で動かすプレイヤーを1つ描く。
// どちらもシーンのアクター(Actor の子クラス)で、見た目は SpriteComponent で足す。
// 更新は Scene::Tick、描画は Scene::ForEachActor で、SpriteComponent を持つアクターだけを積む。
// SDL は直接使わず、platform と render だけを通す(main からは SDL のヘッダが見えない設定にしてある)。

#include "core/ObjectRegistry.h"
#include "engine/FrameClock.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include "platform/Image.h"
#include "platform/Platform.h"
#include "platform/Window.h"
#include "render/RenderDevice.h"

#include <cstdio> // std::printf
#include <random> // std::mt19937(乱数)
#include <vector>

namespace
{
    namespace core = husky::core;
    namespace platform = husky::platform;
    namespace engine = husky::engine;
    namespace render = husky::render;

    constexpr int ScreenWidth = 1280;
    constexpr int ScreenHeight = 720;

    constexpr int BouncerCount = 1000;
    constexpr float BouncerSize = 32.0f;  // テクスチャと同じ大きさ(拡大なし)
    constexpr float PlayerSize = 128.0f;  // 4 倍に拡大

    // 1つの軸について、位置を進め、画面の端に当たったら跳ね返す。
    // position と velocity は参照(&)なので、呼んだ側の変数が書き換わる。
    // (Actor の位置が float なので、S-2 までの double から float に変えた。画面の大きさなら精度は足りる)
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

    // スプライト1枚の見た目を、アクターに足すコンポーネント。描画に要る大きさと色を持つ。
    // 位置は持たない。描くときは、持ち主のアクターの位置(x, y)を使う。
    // Tick は使わない(見た目は描画のときに読まれるだけ)。
    // (engine はまだ render に依存していないので、今はゲーム側のここに置く。engine に移すかは後で決める)
    // 動作スレッド: ゲームスレッドのみ
    class SpriteComponent : public engine::Component
    {
        HUSKY_OBJECT(SpriteComponent, engine::Component)
    public:
        float width = 32.0f; // 描く大きさ(ピクセル)
        float height = 32.0f;
        render::Color color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャに掛ける色。白ならテクスチャのまま

        // color は render の型で、リフレクションの対応型ではないので登録しない。
        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(width)
            HUSKY_PROPERTY(height)
        HUSKY_REFLECTION_END()
    };

    // 画面の中を跳ね回るアクター。見た目は SpriteComponent で足す。
    // 動作スレッド: ゲームスレッドのみ
    class BouncerActor : public engine::Actor
    {
        HUSKY_OBJECT(BouncerActor, engine::Actor)
    public:
        float vx = 0.0f; // 速度(ピクセル/秒)
        float vy = 0.0f;

        // Scene::Tick から毎フレーム呼ばれる。
        // override: 親(Actor)の virtual 関数を上書きしていることを、コンパイラに確かめさせる(C# の override と同じ)。
        // 名前や引数を間違えると、上書きではなく「別の関数」になってしまうが、override があればコンパイルエラーになる。
        // 跳ね返る位置は、見た目(SpriteComponent)の大きさではなく、当たりの大きさ(BouncerSize)で決める。
        // 動きは見た目に頼らない(見た目のコンポーネントを外しても、同じように動く)。
        void Tick(double dt) override
        {
            const float step = static_cast<float>(dt);
            MoveAndBounce(x, vx, step, ScreenWidth - BouncerSize);
            MoveAndBounce(y, vy, step, ScreenHeight - BouncerSize);
        }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(vx)
            HUSKY_PROPERTY(vy)
        HUSKY_REFLECTION_END()
    };

    // 矢印キー(または WASD)で動くプレイヤーのアクター。見た目は SpriteComponent で足す。
    // 動作スレッド: ゲームスレッドのみ
    class PlayerActor : public engine::Actor
    {
        HUSKY_OBJECT(PlayerActor, engine::Actor)
    public:
        float moveSpeed = 300.0f; // 移動の速さ(ピクセル/秒)

        // キーの状態を読む先。借りているだけで、所有はしていない(Input の寿命はゲームループが決める)。
        // Input は GC 管理の型ではないので、ハンドルではなくポインタで持つ。プロパティにも登録しない。
        // (入力をどうアクターに渡すかは、入力の仕組みを作るときに考え直す。今は仮)
        const engine::Input* input = nullptr;

        void Tick(double dt) override
        {
            if (input == nullptr)
            {
                return;
            }
            // 押されている方向キーから、動く向きを決める(-1, 0, +1)。
            float moveX = 0.0f;
            float moveY = 0.0f;
            if (input->IsDown(engine::Key::Left) || input->IsDown(engine::Key::A))  { moveX -= 1.0f; }
            if (input->IsDown(engine::Key::Right) || input->IsDown(engine::Key::D)) { moveX += 1.0f; }
            if (input->IsDown(engine::Key::Up) || input->IsDown(engine::Key::W))    { moveY -= 1.0f; } // 上は -y
            if (input->IsDown(engine::Key::Down) || input->IsDown(engine::Key::S))  { moveY += 1.0f; }

            // 速さ × dt = このフレームで進む距離。フレームレートが違っても、1秒で進む距離は同じになる。
            const float step = moveSpeed * static_cast<float>(dt);
            x += moveX * step;
            y += moveY * step;
        }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(moveSpeed)
        HUSKY_REFLECTION_END()
    };

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
            SpriteComponent* sprite = bouncer != nullptr ? bouncer->AddComponent<SpriteComponent>() : nullptr;
            if (sprite == nullptr) // オブジェクト用のメモリを使い切った
            {
                std::printf("跳ね回るアクターを作れませんでした(%d 個目)\n", i);
                return;
            }
            bouncer->x = static_cast<float>(randomX(random));
            bouncer->y = static_cast<float>(randomY(random));
            bouncer->vx = static_cast<float>(randomSpeed(random));
            bouncer->vy = static_cast<float>(randomSpeed(random));
            sprite->width = BouncerSize;
            sprite->height = BouncerSize;
            sprite->color = {randomColor(random), randomColor(random), randomColor(random), 1.0f};
        }
    }

    // ゲームループ本体。終了が要求されるまで戻らない。
    // 引数は参照(&)で受け取る。コピーではなく、main が持っている本物を使うため。
    void RunGameLoop(render::RenderDevice& renderDevice)
    {
        engine::FrameClock clock; // dt を決めるのはこれだけ
        engine::Input input;

        // キーイベントの受け皿。ループの外に置いて毎フレーム使い回し、メモリの確保を減らす。
        std::vector<platform::KeyEvent> keyEvents;

        // ゲームオブジェクトの登録簿と、シーン。シーンを GC の根にする。
        // registry は input より後に宣言しているので、input より先に破棄される(C++ では後に作ったものが先に壊れる)。
        // そのため、プレイヤーが input を借りている間に、input が先に消えることはない。
        core::ObjectRegistry registry;
        engine::Scene* scene = registry.Create<engine::Scene>();
        registry.AddRoot(scene->GetHandle());

        // GC は毎フレームではなく、1秒に1回だけ回す(停止型なので、回すたびに少し時間がかかるため)。
        constexpr double GcIntervalSeconds = 1.0;
        double secondsSinceGc = 0.0;
        std::size_t collectedLastGc = 0;

        // Space で切り替える2色。
        const render::Color colors[] = {
            {0.10f, 0.12f, 0.20f, 1.0f}, // 紺
            {0.20f, 0.45f, 0.30f, 1.0f}, // 緑
        };
        int colorIndex = 0;

        // アクターを置く。一覧の順番が描画の順番なので、プレイヤーを最後に置いて一番手前に描く。
        SpawnBouncers(*scene, BouncerCount);
        if (PlayerActor* player = scene->Spawn<PlayerActor>())
        {
            if (SpriteComponent* sprite = player->AddComponent<SpriteComponent>())
            {
                sprite->width = PlayerSize;
                sprite->height = PlayerSize; // 色は既定の白(テクスチャのまま)
            }
            player->x = (ScreenWidth - PlayerSize) / 2.0f; // 画面の中央
            player->y = (ScreenHeight - PlayerSize) / 2.0f;
            player->input = &input;
        }

        // FPS 表示用の集計(1秒ごとにリセットする)
        int framesThisSecond = 0;
        double secondsThisSecond = 0.0;

        // ゲームループ。1周 = 1フレーム。
        while (true)
        {
            // 1. このフレームの dt を決める
            const double dt = clock.Tick(platform::GetTimeNanoseconds());

            // 2. 入力: OS のイベントを処理し、キーの状態を更新する
            const bool quitRequested = platform::ProcessEvents(keyEvents);
            input.BeginFrame(keyEvents);
            if (quitRequested || input.WasPressed(engine::Key::Escape))
            {
                break;
            }

            // 3. 更新
            if (input.WasPressed(engine::Key::Space))
            {
                colorIndex = 1 - colorIndex; // 0 と 1 を入れ替える
                std::printf("色を切り替えました: %d\n", colorIndex);
            }

            scene->Tick(dt); // シーンの全アクターを更新(跳ね回るアクターとプレイヤー)

            // 3.5 フレームの終わり: 破棄の確定と、必要なら GC。
            //     この順番が大事: Destroy 済みのアクターを一覧から外す → 破棄を確定する → (たまに)GC。
            scene->RemoveDestroyedActors();
            registry.FlushDestroyed();
            secondsSinceGc += dt;
            if (secondsSinceGc >= GcIntervalSeconds)
            {
                collectedLastGc = registry.CollectGarbage();
                secondsSinceGc = 0.0;
            }

            ++framesThisSecond;
            secondsThisSecond += dt;
            if (secondsThisSecond >= 1.0)
            {
                const render::FrameStats stats = renderDevice.GetLastFrameStats();
                std::printf("FPS: %d  平均 dt: %.2f ms  スプライト: %d  描画命令: %d 回  描けなかった数: %d"
                            "  オブジェクト: %zu  直前の GC で回収: %zu\n",
                            framesThisSecond,
                            secondsThisSecond / framesThisSecond * 1000.0,
                            stats.spriteCount, stats.drawCallCount, stats.droppedSpriteCount,
                            registry.GetObjectCount(), collectedLastGc);
                std::fflush(stdout); // 出力を溜めずに、すぐ画面(やファイル)へ書き出す
                framesThisSecond = 0;
                secondsThisSecond = 0.0;
            }

            // 4. 描画: 描きたいスプライトを積んで、EndFrame でまとめて描く。
            //    vsync のため BeginFrame の中で待つ。
            if (renderDevice.BeginFrame(colors[colorIndex]))
            {
                // シーンの一覧の順に積む。先に積んだものが奥、後に積んだものが手前になる。
                // [&](...) { ... } はラムダ式(C# の (actor) => { ... })。
                // [&] は「外の変数(renderDevice)を参照で使う」という意味。C# のラムダは自動でこうなる。
                scene->ForEachActor([&](engine::Actor& actor) {
                    // Unity の GetComponent<SpriteRenderer>() と同じ。スプライトを付けていないアクターは描かない。
                    // 位置はアクターから、大きさと色はコンポーネントから取る。
                    const SpriteComponent* spriteComponent = actor.GetComponent<SpriteComponent>();
                    if (spriteComponent == nullptr)
                    {
                        return;
                    }
                    render::Sprite sprite;
                    sprite.x = actor.x;
                    sprite.y = actor.y;
                    sprite.width = spriteComponent->width;
                    sprite.height = spriteComponent->height;
                    sprite.color = spriteComponent->color;
                    renderDevice.DrawSprite(sprite);
                });

                renderDevice.EndFrame();
            }
        }
    }
}

int main()
{
    if (!platform::Initialize())
    {
        return 1; // 0 以外は「異常終了」の意味
    }

    int exitCode = 0;

    // この { } がウィンドウの寿命になる。
    // その後で platform::Shutdown() を呼ぶので、「ウィンドウを壊してから SDL を終了する」順番が必ず守られる。
    {
        platform::Window window;

        platform::WindowDesc desc;
        desc.title = "HuskyEngineV2";
        desc.width = ScreenWidth;
        desc.height = ScreenHeight;

        if (!window.Open(desc))
        {
            exitCode = 1;
        }
        else
        {
            // renderDevice は window より内側のブロックにあるので、window より先に破棄される。
            // RenderDevice は Window を使っているので、この順番でないといけない。
            render::RenderDevice renderDevice;

            // テクスチャは2段で用意する: ① PNG を CPU のメモリに読む(platform) → ② GPU に送る(render)
            // image は GPU に送り終えたら要らないので、この if の中だけで使う。
            platform::Image image;
            if (renderDevice.Initialize(window)
                && platform::LoadPng("assets/textures/test_sprite.png", image)
                && renderDevice.LoadTestTexture(image))
            {
                RunGameLoop(renderDevice);
            }
            else
            {
                exitCode = 1;
            }
        } // ← ここで renderDevice が破棄される
    } // ← ここで window が破棄される

    platform::Shutdown();
    return exitCode;
}
