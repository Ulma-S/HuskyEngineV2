// HuskyEngineV2 のエントリポイント(プログラムが最初に実行する関数)。
// 動作スレッド: メインスレッド(= ゲームスレッド)。
//
// ウィンドウを1枚開き、閉じるボタンか Escape が押されるまでゲームループを回す。
// 画面は毎フレーム1色で塗りつぶし、Space を押すと色が切り替わる。
// 跳ね回るスプライトを 1000 個と、矢印キー(または WASD)で動かすプレイヤーを1つ描く。
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

    // 画面の中を跳ね回るスプライト1つ分の状態。
    struct Bouncer
    {
        double x = 0.0, y = 0.0;   // 左上の位置(ピクセル)
        double vx = 0.0, vy = 0.0; // 速度(ピクセル/秒)
        render::Color color;       // テクスチャに掛ける色
    };

    constexpr int BouncerCount = 1000;
    constexpr double BouncerSize = 32.0;  // テクスチャと同じ大きさ(拡大なし)
    constexpr double PlayerSize = 128.0;  // 4 倍に拡大

    // 跳ね回るスプライトを count 個作る。
    // 乱数の種(seed)を固定しているので、毎回同じ配置になる(CLAUDE.md: 結果が毎回同じになるように)。
    std::vector<Bouncer> CreateBouncers(int count)
    {
        std::mt19937 random(12345); // 種を固定した乱数生成器
        std::uniform_real_distribution<double> randomX(0.0, ScreenWidth - BouncerSize);
        std::uniform_real_distribution<double> randomY(0.0, ScreenHeight - BouncerSize);
        std::uniform_real_distribution<double> randomSpeed(-200.0, 200.0);
        std::uniform_real_distribution<float> randomColor(0.4f, 1.0f);

        std::vector<Bouncer> bouncers(static_cast<std::size_t>(count));
        for (Bouncer& b : bouncers) // & で受けると、コピーではなく vector の中身を直接書き換えられる
        {
            b.x = randomX(random);
            b.y = randomY(random);
            b.vx = randomSpeed(random);
            b.vy = randomSpeed(random);
            b.color = {randomColor(random), randomColor(random), randomColor(random), 1.0f};
        }
        return bouncers;
    }

    // 1つの軸について、位置を進め、画面の端に当たったら跳ね返す。
    // position と velocity は参照(&)なので、呼んだ側の変数が書き換わる。
    void MoveAndBounce(double& position, double& velocity, double dt, double maxPosition)
    {
        position += velocity * dt;
        if (position < 0.0)
        {
            position = 0.0;
            velocity = -velocity;
        }
        else if (position > maxPosition)
        {
            position = maxPosition;
            velocity = -velocity;
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
        // (アクターはまだいない。跳ね回るスプライトをアクターにするのは次のステップ)
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

        std::vector<Bouncer> bouncers = CreateBouncers(BouncerCount);

        // プレイヤーの位置(ピクセル、左上)と、移動の速さ(ピクセル/秒)。
        double quadX = (ScreenWidth - PlayerSize) / 2.0; // 画面の中央
        double quadY = (ScreenHeight - PlayerSize) / 2.0;
        const double moveSpeed = 300.0;

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

            // 押されている方向キーから、動く向きを決める(-1, 0, +1)。
            double moveX = 0.0;
            double moveY = 0.0;
            if (input.IsDown(engine::Key::Left) || input.IsDown(engine::Key::A))  { moveX -= 1.0; }
            if (input.IsDown(engine::Key::Right) || input.IsDown(engine::Key::D)) { moveX += 1.0; }
            if (input.IsDown(engine::Key::Up) || input.IsDown(engine::Key::W))    { moveY -= 1.0; } // 上は -y
            if (input.IsDown(engine::Key::Down) || input.IsDown(engine::Key::S))  { moveY += 1.0; }

            // 速さ × dt = このフレームで進む距離。フレームレートが違っても、1秒で進む距離は同じになる。
            quadX += moveX * moveSpeed * dt;
            quadY += moveY * moveSpeed * dt;

            for (Bouncer& b : bouncers)
            {
                MoveAndBounce(b.x, b.vx, dt, ScreenWidth - BouncerSize);
                MoveAndBounce(b.y, b.vy, dt, ScreenHeight - BouncerSize);
            }

            scene->Tick(dt); // シーンの全アクターを更新

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
                // 先に積んだものが奥、後に積んだものが手前になる。
                for (const Bouncer& b : bouncers)
                {
                    render::Sprite sprite;
                    sprite.x = static_cast<float>(b.x);
                    sprite.y = static_cast<float>(b.y);
                    sprite.width = static_cast<float>(BouncerSize);
                    sprite.height = static_cast<float>(BouncerSize);
                    sprite.color = b.color;
                    renderDevice.DrawSprite(sprite);
                }

                // プレイヤーは最後に積むので、一番手前に描かれる。
                render::Sprite player;
                player.x = static_cast<float>(quadX);
                player.y = static_cast<float>(quadY);
                player.width = static_cast<float>(PlayerSize);
                player.height = static_cast<float>(PlayerSize);
                renderDevice.DrawSprite(player);

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
