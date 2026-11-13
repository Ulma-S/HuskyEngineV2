// HuskyEngineV2 のエントリポイント(プログラムが最初に実行する関数)。ゲームを起動する exe。
// 動作スレッド: メインスレッド(= ゲームスレッド)。
//
// ゲームのコード(アクターの型、型の登録、最初の配置)は src/game にある(game/Game.h)。ここは起動するだけ:
// アセットとシーンを用意し、Space で背景色を切り替える処理と開発用の UI を渡して、engine::Application に動かしてもらう。
// ゲームループ、ウィンドウ、描画、フレームの終わりの処理(破棄の確定と GC)は Application の中にある。

#include "engine/Application.h"
#include "engine/Input.h"
#include "game/Game.h"
#include "platform/FileSystem.h"
#include "platform/MemoryHooks.h"
#include "render/RenderDevice.h"

#if HUSKY_DEVELOPMENT
#include "tools/DebugWindow.h" // 開発用のデバッグの窓(tools モジュール。開発ビルドだけ)
#include "tools/EditorCommands.h" // エディタのコマンド(同じく開発ビルドだけ)
#include "tools/EditorPlayMode.h" // 編集モードと Play / Stop(同じく開発ビルドだけ)
#include "tools/Inspector.h"   // シーンのインスペクタ(同じく開発ビルドだけ)
#endif

#include <cstdio> // std::printf
#include <string>
#include <utility> // std::move

namespace
{
    namespace engine = husky::engine;
    namespace game = husky::game;
    namespace platform = husky::platform;
#if HUSKY_DEVELOPMENT
    namespace tools = husky::tools;
#endif
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
    game::RegisterGameTypes(app.GetTypeRegistry());

    // アセットのフォルダ(GUID → パスの表、シーンのファイル、すべての読み書きの起点)。
    //   開発ビルド: ソースの assets を直接読み書きする(Unity のエディタが、プロジェクトの Assets を直接読むのと同じ)。
    //              足した・直したファイルがビルドし直さなくても見え、エディタが書くもの(シーン、.meta)も1か所で済む。
    //   製品(Release): ビルドのたびに exe の隣へコピーされる assets を読む(書き込みはしない)。
#if HUSKY_DEVELOPMENT
    const std::string assetsDirectory = std::string(HUSKY_SOURCE_DIR) + "assets";
#else
    const std::string assetsDirectory = platform::GetExecutableDirectory() + "assets";
#endif
    std::printf("アセットのフォルダ: %s\n", assetsDirectory.c_str());
    const std::string scenePath = assetsDirectory + "/scenes/main.json";

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】エディタのコマンド。ボタン、ショートカットのキー(将来は CLI や AI も)は、どれも名前で呼ぶだけ。
    // main の中に1つだけ作り、参照で渡す(Run が終わるまで生きている)。
    // 編集モード(Play / Stop)は、コマンドより先に作る。コマンドが playMode を覚えるので、C++ のルール
    // 「後に作ったものから先に壊れる」により、コマンドの一覧が先に壊れ、playMode を使う人が残らない。
    tools::EditorPlayMode playMode;
    tools::EditorCommandRegistry commands;
    // 再生・一時停止・コマ送り・停止(editor.play / pause / step / stop)。画面では1行に並ぶ。
    playMode.RegisterCommands(commands);
    // アセットを最新にする(asset.refresh): .meta を作り、スプライトシートを整え、アセットの表を作り直す。
    commands.Register(tools::MakeRefreshAssetsCommand(assetsDirectory));
    // シーンの保存(scene.save、F5)。開発ビルドは読むのもソースの assets なので、書く先はそこ1か所だけ
    // (次の起動からそのまま読まれ、次のビルドで exe の隣にもコピーされる)。
    // 再生中は保存できないようにする(動いた後の位置などを、うっかり編集の結果として保存しないように)。
    tools::EditorCommand saveScene = tools::MakeSaveSceneCommand({scenePath});
    saveScene.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(saveScene));
    // アクターを置く(actor.spawn)。引数が要るので、画面に入力欄ができるまでは CLI から使う(ボタンは灰色)。
    // 再生中に置いても Stop で消えるので、編集中だけにする。
    tools::EditorCommand spawnActor = tools::MakeSpawnActorCommand();
    spawnActor.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(spawnActor));
    // 消す(actor.destroy)と、その Undo で使う戻す(actor.restore)。置くと同じく、編集中だけ。
    tools::EditorCommand destroyActor = tools::MakeDestroyActorCommand();
    destroyActor.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(destroyActor));
    tools::EditorCommand restoreActor = tools::MakeRestoreActorCommand();
    restoreActor.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(restoreActor));
    // コンポーネントを足す・外す(component.add / component.remove。PF-7c)。互いの Undo になる。編集中だけ。
    tools::EditorCommand addComponent = tools::MakeAddComponentCommand();
    addComponent.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(addComponent));
    tools::EditorCommand removeComponent = tools::MakeRemoveComponentCommand();
    removeComponent.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(removeComponent));
    // 選んだアクターからプレハブを作る(prefab.create)。ソースの assets に書く。再生中の値を写さないよう、編集中だけ。
    tools::EditorCommand createPrefab = tools::MakeCreatePrefabCommand(assetsDirectory);
    createPrefab.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(createPrefab));
    // アクターの一覧(actor.list。読むだけ)と、プロパティの書き換え(property.set。編集中だけ)。
    commands.Register(tools::MakeListActorsCommand());
    tools::EditorCommand setProperty = tools::MakeSetPropertyCommand();
    setProperty.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(setProperty));
    // まとめて書き換える(property.setMany)。prefab.revert の Undo もこれを呼ぶので、編集中だけ(property.set と同じ)。
    tools::EditorCommand setManyProperties = tools::MakeSetManyPropertiesCommand();
    setManyProperties.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(setManyProperties));
    // プレハブとの違いの一覧(prefab.overrides。読むだけ)と、プレハブに戻す(prefab.revert。編集中だけ)。
    commands.Register(tools::MakeListOverridesCommand());
    tools::EditorCommand revertPrefab = tools::MakeRevertPrefabCommand();
    revertPrefab.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(revertPrefab));
    // プレハブに書き戻す(prefab.apply)。ソースの assets のプレハブを書き換える。編集中だけ。
    tools::EditorCommand applyPrefab = tools::MakeApplyPrefabCommand(assetsDirectory);
    applyPrefab.whyUnavailable = [&playMode](engine::Application&) { return playMode.WhyNotEditing(); };
    commands.Register(std::move(applyPrefab));
    // 元に戻す・やり直す(editor.undo / editor.redo)。履歴は commands が持つ。戻すコマンドが使えないとき(再生中など)は灰色。
    commands.Register(tools::MakeUndoCommand(commands));
    commands.Register(tools::MakeRedoCommand(commands));

    // 起動時のアセットの準備も、ボタンと同じコマンドで行う(手順が1か所にしか無いので、起動時とボタンで食い違わない)。
    commands.Execute("asset.refresh", app);
#else
    // 製品: ファイルは直さず(エディタの役目)、表を作るだけ。
    engine::AssetScanReport assetReport;
    app.GetAssetDatabase().Scan(assetsDirectory, &assetReport);
    std::printf("アセット: %zu 個(.meta が無い %zu、読めない .meta %zu、対の無い .meta %zu、GUID の重なり %zu)\n",
                assetReport.assetCount, assetReport.missingMetaCount, assetReport.invalidMetaCount,
                assetReport.orphanMetaCount, assetReport.duplicateGuidCount);
#endif

    // 最初のシーン: シーンのファイルがあれば読み込む。無ければ、コードで置く(ファイルは、開発ビルドの保存のコマンドで作る)。
    if (!platform::FileExists(scenePath.c_str()) || !app.LoadSceneFile(scenePath))
    {
        std::printf("シーンのファイルが無いので、コードでアクターを置きます(%s)\n", scenePath.c_str());
        game::SpawnInitialActors(app);
    }

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】編集の状態で始める(Unity と同じ)。置いたものは、Play を押すまで動かない。
    playMode.EnterEditMode(app);

    // 開発用の UI: 毎フレーム、tools のデバッグの窓(状態とコマンドのボタンつき)とインスペクタを作る。
    // どちらも覚えておくもの(入力した引数、選んでいるアクター)があるので、main の中に1つだけ作り、参照で渡す。
    tools::DebugWindow debugWindow;
    tools::SceneInspector inspector;
    app.SetDevUiCallback([&commands, &debugWindow, &inspector, &playMode](engine::Application& application) {
        debugWindow.Draw(application, &commands, &playMode);
        inspector.Draw(application, &commands); // 値の書き換えは property.set を通す(Undo できる)
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
    desc.width = game::ScreenWidth;
    desc.height = game::ScreenHeight;
    return app.Run(desc); // 閉じるボタンか Escape で戻ってくる
}
