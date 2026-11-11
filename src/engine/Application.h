// アプリケーション: ゲームループと、ゲームを動かすのに要るものをまとめて持つ。
// Unity で言えば、エンジンが持っている Player Loop(毎フレームの処理の流れ)にあたる。
// ゲーム側は、最初にアクターを置き、必要なら毎フレームの処理(SetUpdateCallback)を渡して、Run を呼ぶだけ。
//
// 使い方:
//   Application app;
//   app.GetScene().Spawn<Player>();                   // 最初のアクターを置く(ウィンドウを開く前でもよい)
//   app.SetUpdateCallback([](Application& a, double dt) { ... });  // ゲーム固有の毎フレームの処理(無くてもよい)
//   ApplicationDesc desc;
//   desc.title = "My Game";
//   return app.Run(desc);                             // ウィンドウを開き、終了するまでループを回す
//
// テストでは Run を呼ばず、StepFrame で1フレームずつ進める(ウィンドウも GPU も使わない)。
#pragma once

#include "core/ObjectRegistry.h"
#include "engine/FrameClock.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include "platform/Keyboard.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <cstdint>
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
        // DrawSprite で貼るテクスチャ(仮。テクスチャをアセットとして扱えるようになったら消す)。
        std::string testTexturePath = "assets/textures/test_sprite.png";
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
        // シーンを1つ作り、GC の根にし、入力をシーンに渡す。
        explicit Application(std::size_t objectCapacityBytes = core::ObjectRegistry::DefaultCapacityBytes);

        // コピーを禁止する(アプリケーションは1つだけで、中のレジストリやシーンを2つに増やせないため)。
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        core::ObjectRegistry& GetRegistry() { return m_registry; }
        Scene& GetScene() { return *m_scene; }
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
        //   2. 入力を更新する(keyEvents は、このフレームに起きたキーイベント)
        //   3. 更新コールバック → シーンの更新(Scene::Tick)
        //   4. フレームの終わり: Destroy 済みのアクターを一覧から外す → 破棄を確定 → (GcIntervalSeconds ごとに)GC
        // 戻り値: このフレームの dt(秒)。
        double StepFrame(std::uint64_t nowNanoseconds, const std::vector<platform::KeyEvent>& keyEvents);

        // 直前の GC で回収した数(ログ用)。
        std::size_t GetCollectedLastGc() const { return m_collectedLastGc; }

        // platform を初期化し、ウィンドウと描画を用意して、終了が要求されるまでゲームループを回す。
        // 閉じるボタンか Escape で終わる。戻り値: プロセスの終了コード(0 が正常)。
        int Run(const ApplicationDesc& desc);

    private:
        // ウィンドウと描画の用意ができた後の、ゲームループ本体。
        void RunLoop(render::RenderDevice& renderDevice);

        // メンバーは、宣言した順に作られ、逆の順に壊される(C++ の決まり)。
        // m_input をレジストリより先に宣言しているので、シーン(レジストリの中)が m_input を借りている間に、
        // m_input が先に壊れることはない。
        FrameClock m_clock;
        Input m_input;
        core::ObjectRegistry m_registry;
        Scene* m_scene = nullptr; // m_registry が持ち主。ここは借りているだけ

        UpdateCallback m_updateCallback;
        render::Color m_clearColor = {0.10f, 0.12f, 0.20f, 1.0f}; // 紺

        double m_secondsSinceGc = 0.0;
        std::size_t m_collectedLastGc = 0;
    };
}
