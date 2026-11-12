// HuskyEngineV2 のエントリポイント(プログラムが最初に実行する関数)。ゲーム側のコード。
// 動作スレッド: メインスレッド(= ゲームスレッド)。
//
// 跳ね回るスプライトを 1000 個と、矢印キー(または WASD)で動かすプレイヤーを1つ置き、
// Space で背景色を切り替える処理を渡して、engine::Application に動かしてもらう。
// ゲームループ、ウィンドウ、描画、フレームの終わりの処理(破棄の確定と GC)は Application の中にある。

#include "engine/Application.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "engine/SpriteSheetData.h"
#include "platform/FileSystem.h"
#include "platform/MemoryHooks.h"
#include "render/RenderDevice.h"

#if HUSKY_DEVELOPMENT
#include "tools/DebugWindow.h" // 開発用のデバッグの窓(tools モジュール。開発ビルドだけ)
#include "tools/EditorCommands.h" // エディタのコマンド(同じく開発ビルドだけ)
#include "tools/Inspector.h"   // シーンのインスペクタ(同じく開発ビルドだけ)
#endif

#include <cstdint>
#include <cstdio> // std::printf
#include <random> // std::mt19937(乱数)
#include <string>

namespace
{
    namespace engine = husky::engine;
    namespace platform = husky::platform;
#if HUSKY_DEVELOPMENT
    namespace tools = husky::tools;
#endif
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

    // using: engine::SpriteComponent を、この中では SpriteComponent と短く書けるようにする(C# の using と同じ)。
    using engine::SpriteComponent;

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

        void Tick(double dt) override
        {
            // 入力は、自分のいるシーンから引く(配られるのを待たない)。
            // シーンにいない、またはシーンに入力が渡されていなければ、動かない。
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

    // 最初のアクターを置く。一覧の順番が描画の順番なので、プレイヤーを最後に置いて一番手前に描く。
    // 跳ね回るアクターはテクスチャを指さない(既定のテクスチャになる)。プレイヤーだけ、スプライトシート(shapes.spritesheet)の
    // 切り出しを貼る(4 つの図形を並べた shapes.png から切り出したもの)。最初は右下のひし形(diamond)で、
    // アニメーション「cycle」が 4 つの図形を順に切り替える。テクスチャが違うので、描画命令は 2 回になる。
    void SpawnInitialActors(engine::Scene& scene, const engine::AssetDatabase& assets)
    {
        // shapes.spritesheet の「diamond」の切り出しと、「cycle」のアニメーション(4 つの図形を 0.25 秒ずつ順に)の ID
        // (ファイルに書いてある。名前を変えても、この ID は変わらない)。
        constexpr std::int32_t DiamondSpriteId = 1917386538;
        constexpr std::int32_t CycleAnimationId = 1811624963;

        SpawnBouncers(scene, BouncerCount);
        if (PlayerActor* player = scene.Spawn<PlayerActor>())
        {
            if (SpriteComponent* sprite = player->AddComponent<SpriteComponent>())
            {
                sprite->width = PlayerSize;
                sprite->height = PlayerSize; // 色は既定の白(テクスチャのまま)
                // アセットは GUID で指す。読むのは、描くとき(DrawSprites)。
                if (const husky::core::Guid* shapes = assets.FindGuid("sprites/shapes.spritesheet"))
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
            player->x = (ScreenWidth - PlayerSize) / 2.0f; // 画面の中央
            player->y = (ScreenHeight - PlayerSize) / 2.0f;
        }
    }
}

int main()
{
    // 一番最初に: SDL のメモリの確保を、エンジンの入口(MemoryTracker)に通す。
    // SDL が何かを確保するより前でないといけない(platform/MemoryHooks.h)。
    platform::InstallMemoryHooks();

    // ゲームループ、レジストリ、シーン、入力、時計は、すべて Application が持つ。
    // ジョブシステムのワーカーは「コア数 - 1」本(1 本はゲームスレッドの分)。テクスチャの読み込みなどに使う。
    engine::Application app(husky::core::ObjectRegistry::DefaultCapacityBytes, husky::core::JobSystem::RecommendedWorkerCount());

    // ゲームの型を、名前から作れるように登録する(engine の型は Application が登録済み)。
    // シーンをファイルから読み込むときに、"type": "BouncerActor" から本物のオブジェクトを作るのに使う。
    app.GetTypeRegistry().Register<BouncerActor>();
    app.GetTypeRegistry().Register<PlayerActor>();

    // アセットのデータベース(GUID → パスの表)を作る。読むのは、ビルドのたびに exe の隣へコピーされる assets。
    const std::string assetsDirectory = platform::GetExecutableDirectory() + "assets";
#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】.meta が無いアセットに GUID を振る(エディタの役目)。シーンの保存と同じく、2か所に書く。
    //   1. ソースの assets … 残しておくため(git で管理する)
    //   2. exe の隣の assets … ビルドし直さなくても、この起動から表に入るように(1 と同じ GUID を書く)
    engine::CreateMissingMetaFiles(std::string(HUSKY_SOURCE_DIR) + "assets", assetsDirectory);
    // スプライトシート(.spritesheet)を整える: ID を振り、アニメーションのコマの名前を ID に直して書き戻す(これも2か所に書く)。
    engine::NormalizeSpriteSheetsInFolder(std::string(HUSKY_SOURCE_DIR) + "assets", assetsDirectory);
#endif
    engine::AssetScanReport assetReport;
    app.GetAssetDatabase().Scan(assetsDirectory, &assetReport);
    std::printf("アセット: %zu 個(.meta が無い %zu、読めない .meta %zu、対の無い .meta %zu、GUID の重なり %zu)\n",
                assetReport.assetCount, assetReport.missingMetaCount, assetReport.invalidMetaCount,
                assetReport.orphanMetaCount, assetReport.duplicateGuidCount);

    // 最初のシーン: シーンのファイルがあれば読み込む。無ければ、コードで置く(ファイルは、開発ビルドの保存のコマンドで作る)。
    // ファイルは、ビルドのたびに exe の隣へコピーされる assets の中にある。
    const std::string scenePath = platform::GetExecutableDirectory() + "assets/scenes/main.json";
    if (!platform::FileExists(scenePath.c_str()) || !app.LoadSceneFile(scenePath))
    {
        std::printf("シーンのファイルが無いので、コードでアクターを置きます(%s)\n", scenePath.c_str());
        SpawnInitialActors(app.GetScene(), app.GetAssetDatabase());
    }

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】エディタのコマンド。ボタン、ショートカットのキー(将来は CLI や AI も)は、どれも名前で呼ぶだけ。
    // main の中に1つだけ作り、参照で渡す(Run が終わるまで生きている)。
    tools::EditorCommandRegistry commands;
    // シーンの保存(scene.save、F5)。書く先は2か所:
    //   1. ソースの assets/scenes/main.json  … 残しておくため(次のビルドで exe の隣にもコピーされる)
    //   2. exe の隣の assets/scenes/main.json … ビルドし直さなくても、次の起動から読まれるように
    commands.Register(tools::MakeSaveSceneCommand({std::string(HUSKY_SOURCE_DIR) + "assets/scenes/main.json", scenePath}));

    // 開発用の UI: 毎フレーム、tools のデバッグの窓(コマンドのボタンつき)とインスペクタを作る。
    // インスペクタは「選んでいるアクター」を覚えておくので、これも main の中に1つだけ作り、参照で渡す。
    tools::SceneInspector inspector;
    app.SetDevUiCallback([&commands, &inspector](engine::Application& application) {
        tools::DrawDebugWindow(application, &commands);
        inspector.Draw(application);
    });
#endif

    // Space を押すと、背景色を2色で切り替える(ゲーム固有の毎フレームの処理)。
    // [colorIndex = 0] は、ラムダ式が自分用の変数 colorIndex を持つという意味(初期値 0)。
    // mutable を付けると、ラムダ式の中でその変数を書き換えられる(付けないと const 扱いになる)。
#if HUSKY_DEVELOPMENT
    app.SetUpdateCallback([colorIndex = 0, &commands](engine::Application& application, double) mutable {
#else
    app.SetUpdateCallback([colorIndex = 0](engine::Application& application, double) mutable {
#endif
        static const husky::render::Color colors[] = {
            {0.10f, 0.12f, 0.20f, 1.0f}, // 紺
            {0.20f, 0.45f, 0.30f, 1.0f}, // 緑
        };
        if (application.GetInput().WasPressed(engine::Key::Space))
        {
            colorIndex = 1 - colorIndex; // 0 と 1 を入れ替える
            application.SetClearColor(colors[colorIndex]);
            std::printf("色を切り替えました: %d\n", colorIndex);
        }

#if HUSKY_DEVELOPMENT
        // 【開発ビルドだけ】ショートカットのキー(F5 でシーンの保存など)が押されたコマンドを実行する。
        commands.HandleShortcuts(application);
#endif
    });

    engine::ApplicationDesc desc;
    desc.title = "HuskyEngineV2";
    desc.width = ScreenWidth;
    desc.height = ScreenHeight;
    return app.Run(desc); // 閉じるボタンか Escape で戻ってくる
}
