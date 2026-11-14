// アプリケーション: ゲームループと、ゲームを動かすのに要るものをまとめて持つ。
// Unity で言えば、エンジンが持っている Player Loop(毎フレームの処理の流れ)にあたる。
// ゲーム側は、最初にアクターを置き、必要なら毎フレームの処理(SetUpdateCallback)を渡して、Run を呼ぶだけ。
//
// 使い方:
//   Application app;
//   app.GetTypeRegistry().Register<Player>();          // ファイルから作れるように、ゲームの型を登録する
//   app.GetScene().Spawn<Player>();                   // 最初のアクターを置く(ウィンドウを開く前でもよい)
//   app.SetUpdateCallback([](Application& a, double dt) { ... });  // ゲーム固有の毎フレームの処理(無くてもよい)
//   ApplicationDesc desc;
//   desc.title = "My Game";
//   return app.Run(desc);                             // ウィンドウを開き、終了するまでループを回す
//
// テストでは Run を呼ばず、StepFrame で1フレームずつ進める(ウィンドウも GPU も使わない)。
#pragma once

#include "audio/AudioDevice.h"
#include "core/JobSystem.h"
#include "core/ObjectRegistry.h"
#include "core/TypeRegistry.h"
#include "engine/AssetDatabase.h"
#include "engine/AssetLoader.h"
#include "engine/Camera.h"
#include "engine/GameModule.h"
#include "engine/FrameClock.h"
#include "engine/Input.h"
#include "engine/PhysicsScene2D.h"
#include "engine/Scene.h"
#include "engine/TextLayout.h"
#include "engine/UIComponents.h"
#include "platform/InputEvents.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <functional> // std::function(C# の Action<T> や Func<T> にあたる、関数を入れておける型)
#include <string>
#include <utility>    // std::move
#include <vector>

namespace husky::engine
{
    // Run で開くウィンドウなどの設定。
    struct ApplicationDesc
    {
        std::string title = "HuskyEngineV2";
        int width = 1280;  // ウィンドウの中の大きさ(ピクセル)
        int height = 720;
        // テクスチャを指していないスプライトに貼る、既定のテクスチャ。assets のフォルダからの相対パスで、
        // アセットの表(AssetDatabase)で GUID を引いて読む。
        // SpriteComponent::texture が何も指していないスプライトは、これを貼る(白い板ではなく、何か見えるように)。
        std::string defaultSpriteTexture = "textures/test_sprite.png";
        // 【開発ビルドだけ使う】開発用の UI(ImGui)のフォント。assets のフォルダからの相対パスで、アセットの表で引く。
        // 空なら ImGui の既定のフォント(英数字だけ)。BIZ UDゴシック(SIL OFL。ライセンスは assets/fonts/OFL.txt)。
        std::string devUiFont = "fonts/BIZUDGothic-Regular.ttf";
        float devUiFontSize = 15.0f; // ピクセル
    };

    // 動作スレッド: ゲームスレッド(= メインスレッド)のみ
    class Application final
    {
    public:
        // 毎フレーム、入力の更新の後、シーンの更新(Scene::Tick)の前に呼ぶ関数の型。
        // std::function<void(Application&, double)> は「Application& と double を受け取り、何も返さない関数」を入れる箱。
        using UpdateCallback = std::function<void(Application&, double)>;

        // GC を回す間隔(秒)。停止型なので、毎フレームではなく、ときどき回す。
        static constexpr double GcIntervalSeconds = 1.0;

        // objectCapacityBytes: ゲームオブジェクトに使うメモリの大きさ(ObjectRegistry に渡す)。
        // workerCount: ジョブシステムのワーカースレッドの数。既定の 0 は同期実行(テストの結果が毎回同じになる)。
        //   ゲームでは core::JobSystem::RecommendedWorkerCount()(コア数 - 1)を渡す。
        // シーンを1つ作り、GC の根にし、入力をシーンに渡す。
        explicit Application(std::size_t objectCapacityBytes = core::ObjectRegistry::DefaultCapacityBytes,
                             std::size_t workerCount = 0);

        // コピーを禁止する(アプリケーションは1つだけで、中のレジストリやシーンを2つに増やせないため)。
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        core::ObjectRegistry& GetRegistry() { return m_registry; }
        // 型の登録簿(名前からオブジェクトを作る)。engine の型は登録済み。ゲームの型は、Run の前にゲームが登録する。
        core::TypeRegistry& GetTypeRegistry() { return m_types; }
        // 今のゲームのモジュール(HR-3)。exe が起動時に付け(DLL か、静的リンクなら表)、ホットリロードで付け替える。
        GameModuleHost& GetGameModule() { return m_gameModule; }
        Scene& GetScene() { return *m_scene; }

        // 2D の当たり判定(COL-3)。ゲームは GetPhysics2D().GetWorld() で問い合わせる(OverlapPoint、Raycast など)。
        // シーンを更新しているフレームだけ、StepFrame の中で同期と重なりの調べを行う(編集中は止まっている)。
        PhysicsScene2D& GetPhysics2D() { return m_physics2D; }
        // アセットのデータベース(GUID → ファイルのパス)。中身は、ゲームが Run の前に Scan で作る。
        AssetDatabase& GetAssetDatabase() { return m_assets; }
        // アセットのローダ。Run の前にも読める(GPU のテクスチャは、Run の中で描画の準備ができたときに作られる)。
        AssetLoader& GetAssetLoader() { return m_loader; }
        // ジョブシステム(ワーカーの数は、コンストラクタで決める)。ジョブの中では GC 管理のオブジェクトを触らないこと。
        core::JobSystem& GetJobSystem() { return m_jobs; }

        // 音を鳴らす窓口。出す先(スピーカー)を開くのは Run の中だけ。テストと CLI では出す先が無く、Play は何もしない。
        audio::AudioDevice& GetAudio() { return m_audio; }
        const Input& GetInput() const { return m_input; }
        // テストで dt を固定する(FrameClock::SetFixedDelta)ときなどに使う。
        FrameClock& GetClock() { return m_clock; }

        // ゲーム固有の毎フレームの処理を渡す。空の関数(nullptr)を渡すと外せる。
        void SetUpdateCallback(UpdateCallback callback) { m_updateCallback = std::move(callback); }

        // 画面を塗りつぶす色(背景色)。
        void SetClearColor(const render::Color& color) { m_clearColor = color; }
        const render::Color& GetClearColor() const { return m_clearColor; }

        // 1フレーム分の「描画以外」の処理を進める。ウィンドウも GPU も使わないので、テストから呼べる。
        //   1. dt を決める(FrameClock)
        //   2. 入力を更新する(events は、このフレームに起きたキーとマウスのイベントと画面の大きさ)。
        //      読み終わったアセットを書く(AssetLoader::Update)
        //   3. 更新コールバック → シーンの更新(Scene::Tick。SetSceneTickEnabled(false) の間は、コマ送りのときだけ)
        //   4. フレームの終わり: Destroy 済みのアクターを一覧から外す → 破棄を確定 → (GcIntervalSeconds ごとに)GC
        // 戻り値: このフレームの dt(秒)。
        double StepFrame(std::uint64_t nowNanoseconds, const platform::InputEvents& events);
        // キーのイベントだけのフレーム(マウスのイベントは無し、画面の大きさは前のまま)。テストの書きやすさのため。
        double StepFrame(std::uint64_t nowNanoseconds, const std::vector<platform::KeyEvent>& keyEvents);
        // テストで StepFrame(0, {}) や StepFrame(0, {KeyEvent{...}}) と書けるようにする入口。
        // これが無いと、{} は vector<KeyEvent> にも InputEvents にもなれるので、どちらを呼ぶか決まらずコンパイルエラーになる。
        // initializer_list(波かっこの並びそのものの型)は、波かっこからの変換がいちばん近いので、こちらが選ばれる。
        double StepFrame(std::uint64_t nowNanoseconds, std::initializer_list<platform::KeyEvent> keyEvents);

        // シーンの更新(Scene::Tick)を止める・動かす。既定は動かす(製品とテストは、ずっと動かしたまま)。
        // 止めている間も、更新コールバック、アセットの読み込み、フレームの終わりの処理(破棄の確定、GC)、描画は動く。
        // エディタの編集モードと一時停止に使う(tools の EditorPlayMode)。
        void SetSceneTickEnabled(bool enabled) { m_sceneTickEnabled = enabled; }
        bool IsSceneTickEnabled() const { return m_sceneTickEnabled; }
        // 止めている間に、次の StepFrame で1回だけシーンを進める(コマ送り)。動かしている間は意味が無い。
        void RequestSceneStep() { m_sceneStepRequested = true; }

        // 直前の GC で回収した数(ログ用)。
        std::size_t GetCollectedLastGc() const { return m_collectedLastGc; }

        // シーンのファイル(JSON)を読み、中のアクターを今のシーンの後ろに足す(今いるアクターは消さない)。
        // 型の名前は GetTypeRegistry() で探すので、ゲームの型は先に登録しておくこと。
        // path: ファイルのパス。exe の隣の assets を読むときは platform::GetExecutableDirectory() を前に付ける。
        // 戻り値: ファイルを読めて、JSON として正しければ true(中の警告はログに出す。警告があっても true)。
        // ウィンドウも GPU も使わないので、テストからも呼べる。
        bool LoadSceneFile(const std::string& path);

        // シーンのファイルの中身(値の木。SceneAsset::GetData など)から、アクターを今のシーンの後ろに足す(SC-1)。
        // label: ログに出す名前(ファイルのパスなど)。警告はログに出す。
        void AddSceneData(const ArchiveValue& data, const std::string& label);

        // シーンを読むときに、プレハブから置いたアクターの元のプレハブを GUID から探す(SceneSerializer::Read に渡す)。
        // アセットの表で探して読み、読み終わるまで待つ(シーンを読むのは、起動、Stop、CLI のときの1回だけなので)。
        // 表に無い・読めないときは nullptr(読む側が警告を出し、ファイルの値だけで作る)。
        const PrefabAsset* FindPrefabForScene(const core::Guid& guid);

        // 直前のフレームの描画の集計(スプライト数、描画命令の数など)。Run の中で毎フレーム更新される。
        const render::FrameStats& GetLastRenderStats() const { return m_lastRenderStats; }

        // 画面の座標で描く関数を渡す(UI-2a。後のゲーム用 UI の Canvas が使う。製品でも動く)。毎フレーム、シーン(と開発ビルドの
        // ギズモ)の後に、行列を画面ちょうどのカメラ(MakeScreenCamera: 左下が原点、右が +x、上が +y、1 単位 = 1 ピクセル)に
        // 替えてから呼ばれる。中では renderDevice.DrawSprite を呼んでよく、ゲームのカメラが動いても画面の同じ所に出る。
        // 開発用の UI(ImGui)よりは奥。
        using ScreenDrawCallback = std::function<void(render::RenderDevice&)>;
        void SetScreenDrawCallback(ScreenDrawCallback callback) { m_screenDraw = std::move(callback); }

#if HUSKY_DEVELOPMENT
        // 【開発ビルドだけ】開発用の UI を作る関数を渡す。毎フレーム、描画の途中(スプライトを積んだ後)に呼ばれる。
        // この関数の中では ImGui の関数(ImGui::Begin など)を使ってよい。UI の中身は tools モジュールが作る。
        // engine は tools を知らない(依存の向きは tools → engine)ので、関数として受け取る。
        using DevUiCallback = std::function<void(Application&)>;
        void SetDevUiCallback(DevUiCallback callback) { m_devUiCallback = std::move(callback); }

        // 【開発ビルドだけ】シーンのカメラの代わりに、この見え方で映す(エディタのシーンビュー、RS-2)。
        // 編集中はエディタのカメラで自由に見回し、再生中はゲームのカメラに戻す(ClearCameraOverride)。
        // 描画は毎フレームここを読むので、変えると次に描くフレームから効く。
        void SetCameraOverride(const CameraView& view) { m_cameraOverride = view; m_hasCameraOverride = true; }
        void ClearCameraOverride() { m_hasCameraOverride = false; }

        // 【開発ビルドだけ】シーンのスプライトより先に(奥に)描く関数を渡す。毎フレーム、カメラの行列を決めた後に呼ばれる。
        // エディタのグリッド(シーンビュー)を描くのに使う。中では renderDevice.DrawSprite を呼んでよい。
        using BeforeSceneDrawCallback = std::function<void(render::RenderDevice&)>;
        void SetBeforeSceneDrawCallback(BeforeSceneDrawCallback callback) { m_beforeSceneDraw = std::move(callback); }

        // 【開発ビルドだけ】シーンのスプライトの後に(手前に)描く関数を渡す。開発用の UI(ImGui)よりは奥。
        // エディタの移動のギズモ(RS-4)を描くのに使う。中では renderDevice.DrawSprite を呼んでよい。
        void SetAfterSceneDrawCallback(BeforeSceneDrawCallback callback) { m_afterSceneDraw = std::move(callback); }

        // 【開発ビルドだけ】今のシーンを、シーンのファイル(JSON、読みやすい形)に保存する。
        // 保存はエディタの役目なので、製品(Release)にはコードごと入らない。
        // 書き方は platform::WriteTextFile(一時ファイルに書いてから置き換える)なので、途中で止まっても元のファイルは壊れない。
        // 戻り値: 保存できたら true(警告はログに出す)。
        bool SaveSceneFile(const std::string& path);
#endif

        // platform を初期化し、ウィンドウと描画を用意して、終了が要求されるまでゲームループを回す。
        // 閉じるボタンか Escape で終わる。戻り値: プロセスの終了コード(0 が正常)。
        int Run(const ApplicationDesc& desc);

    private:
        // StepFrame の前半(プロファイラーとログのフレームの区切り、dt を決める)と、入力の後の残り。
        // 2つの StepFrame は、入力の渡し方だけが違うので、それ以外をここで共有する。
        double BeginStepFrame(std::uint64_t nowNanoseconds);
        double FinishStepFrame(double dt);
        // initializer_list の StepFrame が、キーのイベントを写す入れ物(毎回使い回す)。
        std::vector<platform::KeyEvent> m_listedKeyEvents;

        // ウィンドウと描画の用意ができた後の、ゲームループ本体。
        void RunLoop(render::RenderDevice& renderDevice);
        // desc.defaultSpriteTexture を読み、根に加える(読めなければ、テクスチャ無しで描く)。
        void LoadDefaultSpriteTexture(const std::string& relativePath);

        // メンバーは、宣言した順に作られ、逆の順に壊される(C++ の決まり)。
        // m_input をレジストリより先に宣言しているので、シーン(レジストリの中)が m_input を借りている間に、
        // m_input が先に壊れることはない。
        // ゲームのモジュール(HR-3)。いちばん先に宣言する: メンバーは宣言と逆の順に壊れるので、これがいちばん後に壊れ、
        // ゲームの型のオブジェクト(レジストリ)と型の登録簿が片付いてから DLL が外れる。
        GameModuleHost m_gameModule;
        core::TypeRegistry m_types; // 型の登録簿(オブジェクトより長く生きるように、レジストリより先に宣言する)
        FrameClock m_clock;
        Input m_input;
        AssetDatabase m_assets;
        // ジョブシステムは、ローダより先に宣言する(ローダは壊れるときに、読み込み中のジョブを待つため)。
        core::JobSystem m_jobs;
        // 音の層。音のアセット(レジストリの中)が回収されるときに波を手放すので、ローダと同じくレジストリより先に宣言する。
        audio::AudioDevice m_audio;
        // ローダは、作ったアセット(レジストリの中)より長く生きる必要がある(アセットが回収されるときにローダを呼ぶ)ので、
        // レジストリより先に宣言する(先に作られ、後に壊される)。
        AssetLoader m_loader;
        PhysicsScene2D m_physics2D; // シーンのコライダーと当たり判定の世界(COL-3)
        core::ObjectRegistry m_registry;
        Scene* m_scene = nullptr; // m_registry が持ち主。ここは借りているだけ

        UpdateCallback m_updateCallback;
        core::ObjectHandle m_defaultSpriteTexture; // 既定のテクスチャのアセット(Run の中で読み、根に加える)
        render::Color m_clearColor = {0.10f, 0.12f, 0.20f, 1.0f}; // 紺

        double m_secondsSinceGc = 0.0;
        std::size_t m_collectedLastGc = 0;
        bool m_sceneTickEnabled = true;    // false の間は Scene::Tick を呼ばない(SetSceneTickEnabled)
        bool m_sceneStepRequested = false; // 止めている間に、次のフレームだけ進める(RequestSceneStep)
        render::FrameStats m_lastRenderStats;
        ScreenDrawCallback m_screenDraw; // 画面の座標で描くもの(SetScreenDrawCallback)
        TextLayoutResult m_uiTextLayout; // UI の文字を並べる入れ物(DrawUI。毎フレーム使い回す)
        UIEventSystem m_uiEvents;        // UI の入力(ボタン。UI-6b)
        bool m_uiEventsActive = false;   // 前のフレームで UI の入力を動かしたか(止めたときに一度だけ Reset する)

#if HUSKY_DEVELOPMENT
        DevUiCallback m_devUiCallback;
        CameraView m_cameraOverride;      // SetCameraOverride の見え方
        bool m_hasCameraOverride = false; // true の間は、シーンのカメラの代わりに m_cameraOverride で映す
        BeforeSceneDrawCallback m_beforeSceneDraw; // シーンより奥に描くもの(SetBeforeSceneDrawCallback)
        BeforeSceneDrawCallback m_afterSceneDraw;  // シーンより手前に描くもの(SetAfterSceneDrawCallback)
#endif
    };
}
