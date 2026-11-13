// Application.h の実装。
#include "engine/Application.h"
#include "engine/EngineLog.h"

#include <cmath> // std::round
#include <utility> // std::move

#include "core/Log.h"
#include "core/Profiler.h"

#include "core/MemoryTracker.h"
#include "engine/CameraComponent.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/SceneSerializer.h"
#include "platform/FileSystem.h"
#include "engine/SpriteComponent.h"
#include "platform/Image.h"
#include "platform/Platform.h"
#include "platform/Window.h"



namespace husky::engine
{
    // コンストラクタの「: m_registry(...)」は、メンバーの初期化の書き方(C# のフィールド初期化子にあたる)。
    // { } の中が走る前に、メンバーは宣言した順に作られている。
    Application::Application(std::size_t objectCapacityBytes, std::size_t workerCount)
        : m_jobs(workerCount)
        , m_loader(m_assets, m_registry, m_jobs, m_types) // m_registry はまだ作られていないが、参照を覚えるだけなのでよい
        , m_registry(objectCapacityBytes)
    {
        RegisterEngineTypes(m_types); // engine の型(Scene、Actor、SpriteComponent など)を、名前から作れるようにする

        // シーンは、レジストリの中で一番最初に作るので、容量が極端に小さくない限り作れる。
        m_scene = m_registry.Create<Scene>();
        m_registry.AddRoot(m_scene->GetHandle());
        m_scene->SetInput(&m_input); // アクターは GetScene()->GetInput() で入力を読む
        m_scene->SetAssetLoader(&m_loader); // アクターは GetScene()->GetAssetLoader() でアセットの参照を引く
    }

    double Application::StepFrame(std::uint64_t nowNanoseconds, const std::vector<platform::KeyEvent>& keyEvents)
    {
        // 0. プロファイラーのフレームの区切り: 前のフレーム(その描画まで)に測った時間をまとめる
        core::Profiler::Get().EndFrame();
        core::Log::Get().AdvanceFrame(); // ログの記録のフレームの番号を進める

        // 1. このフレームの dt を決める(dt を決めるのは FrameClock だけ)
        const double dt = m_clock.Tick(nowNanoseconds);

        // 2. 入力: このフレームのキーイベントで、キーの状態を更新する
        m_input.BeginFrame(keyEvents);

        // 2'. アセット: ワーカーが読み終わったものを、アセットに書く(GPU に送る)。ゲームの処理より前に行い、
        //     このフレームの更新と描画から、読み終わったアセットを使えるようにする。
        {
            HUSKY_PROFILE_SCOPE("Assets");
            m_loader.Update();
        }

        // 3. 更新: ゲーム固有の処理 → シーンの全アクターとコンポーネント
        //    std::function は、中身が入っているかを if で確かめられる(空のまま呼ぶとプログラムが止まる)。
        if (m_updateCallback)
        {
            HUSKY_PROFILE_SCOPE("Update");
            m_updateCallback(*this, dt);
        }
        // シーンの更新を止めている間(エディタの編集中、一時停止中)は、アクターとコンポーネントの Tick を呼ばない。
        // ただし、コマ送りを頼まれたフレームだけは、止めていても 1 回進める。
        if (m_sceneTickEnabled || m_sceneStepRequested)
        {
            {
                HUSKY_PROFILE_SCOPE("Scene.Tick");
                m_scene->Tick(dt);
            }
            // 当たり判定: アクターが動いた後に、コライダーを当たり判定の世界に伝え、重なりを調べてイベントを届ける。
            // 受け取った側は同じフレームのうちに反応でき、Destroy などはこの後のフレームの終わりで確定する。
            m_physics2D.Update(*m_scene, dt); // 中で区間 "Physics2D.*" を測る
            m_sceneStepRequested = false;
        }

        // 4. フレームの終わり: 破棄の確定と、必要なら GC。
        //    この順番が大事: Destroy 済みのアクターを一覧から外す → 破棄を確定する → (たまに)GC。
        HUSKY_PROFILE_SCOPE("FrameEnd"); // ここから関数の終わりまで
        m_scene->RemoveDestroyedActors();
        m_registry.FlushDestroyed();
        m_secondsSinceGc += dt;
        if (m_secondsSinceGc >= GcIntervalSeconds)
        {
            m_collectedLastGc = m_registry.CollectGarbage();
            m_secondsSinceGc = 0.0;
            // 読むだけの ScriptableData が書き換えられていないか(開発ビルドだけ。製品では何もしない)。GC と同じ 1 秒に 1 回。
            m_loader.CheckScriptableDataUnchanged();
        }
        return dt;
    }

    const PrefabAsset* Application::FindPrefabForScene(const core::Guid& guid)
    {
        const PrefabAsset* prefab = m_loader.ResolvePrefab(m_loader.LoadPrefab(guid));
        if (prefab != nullptr && prefab->GetState() == AssetState::Loading)
        {
            m_loader.WaitForAllLoads(); // 読み込み中なら、読み終わるまで待つ(ジョブで読み、ここで結果を受け取る)
        }
        return prefab != nullptr && prefab->IsLoaded() ? prefab : nullptr;
    }

    bool Application::LoadSceneFile(const std::string& path)
    {
        // ファイル → 文字列 → 値の木 → オブジェクト、の順に変換する(どこで失敗しても、ログに理由を出して false)。
        std::string text;
        if (!platform::ReadTextFile(path.c_str(), text))
        {
            return false; // 理由は platform がログに出している
        }
        ArchiveValue file;
        std::string error;
        if (!JsonFormat::Read(text, file, &error))
        {
            HUSKY_LOG(SceneLog::ReadFailed, path, error);
            return false;
        }
        const SceneSerializer::ReadReport report = SceneSerializer::Read(
            file, *m_scene, m_types, [this](const core::Guid& guid) { return FindPrefabForScene(guid); });
        for (const std::string& warning : report.warnings)
        {
            HUSKY_LOG(SceneLog::ReadWarning, path, warning);
        }
        HUSKY_LOG(SceneLog::Loaded, path, report.addedActorCount, report.createdCount, report.warnings.size());
        return true;
    }

#if HUSKY_DEVELOPMENT
    bool Application::SaveSceneFile(const std::string& path)
    {
        // オブジェクト → 値の木 → 文字列 → ファイル、の順に変換する(LoadSceneFile の逆)。
        const SceneSerializer::WriteResult saved = SceneSerializer::Write(*m_scene);
        for (const std::string& warning : saved.warnings)
        {
            HUSKY_LOG(SceneLog::SaveWarning, path, warning);
        }
        std::string text;
        if (!JsonFormat::Write(saved.value, text, true))
        {
            HUSKY_LOG(SceneLog::JsonWriteFailed, path);
            return false;
        }
        if (!platform::WriteTextFile(path.c_str(), text))
        {
            return false; // 理由は platform がログに出している
        }
        HUSKY_LOG(SceneLog::Saved, path, m_scene->actors.size(), text.size());
        return true;
    }
#endif

    int Application::Run(const ApplicationDesc& desc)
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

            platform::WindowDesc windowDesc;
            windowDesc.title = desc.title.c_str();
            windowDesc.width = desc.width;
            windowDesc.height = desc.height;

            if (!window.Open(windowDesc))
            {
                exitCode = 1;
            }
            else
            {
                // renderDevice は window より内側のブロックにあるので、window より先に破棄される。
                // RenderDevice は Window を使っているので、この順番でないといけない。
                render::RenderDevice renderDevice;
#if HUSKY_DEVELOPMENT
                // 開発用の UI のフォント(日本語の字のあるもの)。Initialize の前に渡す。
                if (!desc.devUiFont.empty())
                {
                    const core::Guid* fontGuid = m_assets.FindGuid(desc.devUiFont);
                    std::string fontData;
                    if (fontGuid != nullptr &&
                        platform::ReadTextFile(m_assets.GetFullPath(*fontGuid).c_str(), fontData)) // 中身はそのままのバイト
                    {
                        renderDevice.SetDevUiFont(std::move(fontData), desc.devUiFontSize);
                    }
                    else
                    {
                        HUSKY_LOG(AssetLog::DevUiFontMissing, desc.devUiFont);
                    }
                }
#endif

                if (renderDevice.Initialize(window))
                {
                    // ローダに GPU を付ける。Run より前に読んだテクスチャも、ここで GPU に送られる。
                    m_loader.SetRenderDevice(&renderDevice);
                    LoadDefaultSpriteTexture(desc.defaultSpriteTexture);
                    // 音の出す先を開く。開けなくても(装置が無いなど)、音なしで続ける。
                    m_audio.StartOutput(audio::CreateSdlAudioOutput());
                    RunLoop(renderDevice);
                    m_audio.StopOutput(); // SDL を終える(platform::Shutdown)前に閉じる
                    // RenderDevice を壊す前に、ローダから外す(すべての GPU のテクスチャを手放す)。
                    m_loader.SetRenderDevice(nullptr);
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

    void Application::LoadDefaultSpriteTexture(const std::string& relativePath)
    {
        if (!m_defaultSpriteTexture.IsNull())
        {
            return; // 前の Run で読んだものがある(根に加えてあるので、回収されずに残っている)
        }
        const core::Guid* guid = m_assets.FindGuid(relativePath);
        if (guid == nullptr)
        {
            HUSKY_LOG(AssetLog::DefaultTextureMissing, relativePath);
            return;
        }
        m_defaultSpriteTexture = m_loader.LoadTexture(*guid);
        // ローダが返すハンドルは弱い参照なので、根に加えて、GC に回収されないようにする。
        m_registry.AddRoot(m_defaultSpriteTexture);
    }

    void Application::RunLoop(render::RenderDevice& renderDevice)
    {
        // キーイベントの受け皿。ループの外に置いて毎フレーム使い回し、メモリの確保を減らす。
        std::vector<platform::KeyEvent> keyEvents;

        // FPS 表示用の集計(1秒ごとにリセットする)
        int framesThisSecond = 0;
        double secondsThisSecond = 0.0;
        // 前回のログの時点での、起動からの確保の回数。差を取って「この1秒に何回確保したか」を出す。
        std::uint64_t totalAllocationsAtLastLog = core::GetMemoryStats().totalAllocations;

        // ゲームループ。1周 = 1フレーム。
        while (true)
        {
            // OS のイベントを処理する。閉じるボタンなら終わる。
            if (platform::ProcessEvents(keyEvents))
            {
                break;
            }

            // 描画以外の1フレーム分(dt、入力、更新、フレームの終わり)
            const double dt = StepFrame(platform::GetTimeNanoseconds(), keyEvents);
            if (m_input.WasPressed(Key::Escape))
            {
                break;
            }

            ++framesThisSecond;
            secondsThisSecond += dt;
            if (secondsThisSecond >= 1.0)
            {
                const render::FrameStats stats = renderDevice.GetLastFrameStats();
                const core::MemoryStats memory = core::GetMemoryStats();
                // 小数は 2 桁に丸めて渡す(受け手は %g で書くので、丸めないと 16.6667 のように長くなる)。
                const auto round2 = [](double value) { return std::round(value * 100.0) / 100.0; };
                constexpr double megabyte = 1024.0 * 1024.0;
                HUSKY_LOG(StatsLog::Frame, framesThisSecond, round2(secondsThisSecond / framesThisSecond * 1000.0),
                          stats.spriteCount, stats.drawCallCount, stats.droppedSpriteCount, m_registry.GetObjectCount(),
                          m_collectedLastGc);
                HUSKY_LOG(StatsLog::Memory, round2(static_cast<double>(memory.currentBytes) / megabyte),
                          memory.currentCount, round2(static_cast<double>(memory.peakBytes) / megabyte),
                          memory.totalAllocations - totalAllocationsAtLastLog);
                totalAllocationsAtLastLog = memory.totalAllocations;
#if HUSKY_DEVELOPMENT
                // 区間ごとの処理時間(平均 / この1秒の最大、ミリ秒)。区間ごとに1件(Verbose なので画面には出ない)
                core::Profiler& profiler = core::Profiler::Get();
                for (int i = 0; i < profiler.GetSectionCount(); ++i)
                {
                    const core::Profiler::SectionStats& section = profiler.GetStats(i);
                    HUSKY_LOG(StatsLog::Profile, section.name, round2(section.averageMs), round2(section.maxMs));
                }
                profiler.ResetMax();
#endif
                framesThisSecond = 0;
                secondsThisSecond = 0.0;
            }

            // 描画: 描きたいスプライトを積んで、EndFrame でまとめて描く。vsync のため BeginFrame の中で待つ。
            // 待っている時間も区間にする(ほかの区間が軽いのに FPS が上がらないときは、ここが大きい = 画面の更新を待っている)。
            bool began = false;
            {
                HUSKY_PROFILE_SCOPE("Render.Begin(wait)");
                began = renderDevice.BeginFrame(m_clearColor);
            }
            if (began)
            {
                HUSKY_PROFILE_SCOPE("Render"); // 描画全体(下の区間を含む)
                // カメラ: シーンのカメラ(CameraComponent)があればそれで、無ければ画面ちょうどの動かないカメラで映す。
                // カメラは動き、画面の大きさも窓を広げると変わるので、毎フレーム作り直す。
                CameraMatrices camera = MakeSceneCamera(*m_scene,
                                                              static_cast<float>(renderDevice.GetScreenWidth()),
                                                              static_cast<float>(renderDevice.GetScreenHeight()));
#if HUSKY_DEVELOPMENT
                if (m_hasCameraOverride) // エディタのカメラ(シーンビュー)で映す
                {
                    camera = MakeOrthographicCamera(m_cameraOverride.centerX, m_cameraOverride.centerY, m_cameraOverride.zoom,
                                                    static_cast<float>(renderDevice.GetScreenWidth()),
                                                    static_cast<float>(renderDevice.GetScreenHeight()));
                }
#endif
                renderDevice.SetViewProjection(camera.ViewProjection());

                // SpriteComponent を持つアクターを、シーンの一覧の順に積む。先に積んだものが奥、後に積んだものが手前。
                {
                    HUSKY_PROFILE_SCOPE("Render.Sprites");
#if HUSKY_DEVELOPMENT
                    if (m_beforeSceneDraw) // エディタのグリッドなど、シーンより奥に描くもの
                    {
                        m_beforeSceneDraw(renderDevice);
                    }
#endif
                    DrawSprites(*m_scene, renderDevice, m_loader, m_defaultSpriteTexture);
#if HUSKY_DEVELOPMENT
                    if (m_afterSceneDraw) // エディタの移動のギズモなど、シーンより手前に描くもの
                    {
                        m_afterSceneDraw(renderDevice);
                    }
#endif
                }
#if HUSKY_DEVELOPMENT
                // 開発用の UI: UI のフレームを始めてから、渡された関数(tools の窓)に UI を作らせる。
                if (m_devUiCallback)
                {
                    HUSKY_PROFILE_SCOPE("DevUI");
                    renderDevice.BeginDevUiFrame();
                    m_devUiCallback(*this);
                }
#endif
                {
                    HUSKY_PROFILE_SCOPE("Render.Submit"); // 積んだものを GPU に送る
                    renderDevice.EndFrame();
                }
                m_lastRenderStats = renderDevice.GetLastFrameStats();
            }
        }
    }
}
