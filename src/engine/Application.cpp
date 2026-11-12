// Application.h の実装。
#include "engine/Application.h"

#include "core/MemoryTracker.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/SceneSerializer.h"
#include "platform/FileSystem.h"
#include "engine/SpriteComponent.h"
#include "platform/Image.h"
#include "platform/Platform.h"
#include "platform/Window.h"

#include <cstdio> // std::printf

namespace husky::engine
{
    // コンストラクタの「: m_registry(...)」は、メンバーの初期化の書き方(C# のフィールド初期化子にあたる)。
    // { } の中が走る前に、メンバーは宣言した順に作られている。
    Application::Application(std::size_t objectCapacityBytes)
        : m_loader(m_assets, m_registry) // m_registry はまだ作られていないが、参照を覚えるだけなのでよい
        , m_registry(objectCapacityBytes)
    {
        RegisterEngineTypes(m_types); // engine の型(Scene、Actor、SpriteComponent など)を、名前から作れるようにする

        // シーンは、レジストリの中で一番最初に作るので、容量が極端に小さくない限り作れる。
        m_scene = m_registry.Create<Scene>();
        m_registry.AddRoot(m_scene->GetHandle());
        m_scene->SetInput(&m_input); // アクターは GetScene()->GetInput() で入力を読む
    }

    double Application::StepFrame(std::uint64_t nowNanoseconds, const std::vector<platform::KeyEvent>& keyEvents)
    {
        // 1. このフレームの dt を決める(dt を決めるのは FrameClock だけ)
        const double dt = m_clock.Tick(nowNanoseconds);

        // 2. 入力: このフレームのキーイベントで、キーの状態を更新する
        m_input.BeginFrame(keyEvents);

        // 3. 更新: ゲーム固有の処理 → シーンの全アクターとコンポーネント
        //    std::function は、中身が入っているかを if で確かめられる(空のまま呼ぶとプログラムが止まる)。
        if (m_updateCallback)
        {
            m_updateCallback(*this, dt);
        }
        m_scene->Tick(dt);

        // 4. フレームの終わり: 破棄の確定と、必要なら GC。
        //    この順番が大事: Destroy 済みのアクターを一覧から外す → 破棄を確定する → (たまに)GC。
        m_scene->RemoveDestroyedActors();
        m_registry.FlushDestroyed();
        m_secondsSinceGc += dt;
        if (m_secondsSinceGc >= GcIntervalSeconds)
        {
            m_collectedLastGc = m_registry.CollectGarbage();
            m_secondsSinceGc = 0.0;
        }
        return dt;
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
            std::printf("シーンのファイルを読めません(%s): %s\n", path.c_str(), error.c_str());
            return false;
        }
        const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *m_scene, m_types);
        for (const std::string& warning : report.warnings)
        {
            std::printf("シーンの読み込みの警告(%s): %s\n", path.c_str(), warning.c_str());
        }
        std::printf("シーンを読み込みました(%s): アクター %zu、オブジェクト %zu、警告 %zu\n", path.c_str(),
                    report.addedActorCount, report.createdCount, report.warnings.size());
        return true;
    }

#if HUSKY_DEVELOPMENT
    bool Application::SaveSceneFile(const std::string& path)
    {
        // オブジェクト → 値の木 → 文字列 → ファイル、の順に変換する(LoadSceneFile の逆)。
        const SceneSerializer::WriteResult saved = SceneSerializer::Write(*m_scene);
        for (const std::string& warning : saved.warnings)
        {
            std::printf("シーンの保存の警告(%s): %s\n", path.c_str(), warning.c_str());
        }
        std::string text;
        if (!JsonFormat::Write(saved.value, text, true))
        {
            std::printf("シーンを JSON にできません(%s)\n", path.c_str());
            return false;
        }
        if (!platform::WriteTextFile(path.c_str(), text))
        {
            return false; // 理由は platform がログに出している
        }
        std::printf("シーンを保存しました(%s): アクター %zu、%zu バイト\n", path.c_str(), m_scene->actors.size(),
                    text.size());
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

                if (renderDevice.Initialize(window))
                {
                    // ローダに GPU を付ける。Run より前に読んだテクスチャも、ここで GPU に送られる。
                    m_loader.SetRenderDevice(&renderDevice);
                    LoadDefaultSpriteTexture(desc.defaultSpriteTexture);
                    RunLoop(renderDevice);
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
            std::printf("既定のテクスチャがアセットの表にありません(%s)。テクスチャ無しで描きます\n", relativePath.c_str());
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
                std::printf("FPS: %d  平均 dt: %.2f ms  スプライト: %d  描画命令: %d 回  描けなかった数: %d"
                            "  オブジェクト: %zu  直前の GC で回収: %zu"
                            "  ヒープ: %.2f MB(%zu 件、最大 %.2f MB、この1秒の確保 %llu 回)\n",
                            framesThisSecond,
                            secondsThisSecond / framesThisSecond * 1000.0,
                            stats.spriteCount, stats.drawCallCount, stats.droppedSpriteCount,
                            m_registry.GetObjectCount(), m_collectedLastGc,
                            static_cast<double>(memory.currentBytes) / (1024.0 * 1024.0), memory.currentCount,
                            static_cast<double>(memory.peakBytes) / (1024.0 * 1024.0),
                            static_cast<unsigned long long>(memory.totalAllocations - totalAllocationsAtLastLog));
                totalAllocationsAtLastLog = memory.totalAllocations;
                std::fflush(stdout); // 出力を溜めずに、すぐ画面(やファイル)へ書き出す
                framesThisSecond = 0;
                secondsThisSecond = 0.0;
            }

            // 描画: 描きたいスプライトを積んで、EndFrame でまとめて描く。vsync のため BeginFrame の中で待つ。
            if (renderDevice.BeginFrame(m_clearColor))
            {
                // SpriteComponent を持つアクターを、シーンの一覧の順に積む。先に積んだものが奥、後に積んだものが手前。
                DrawSprites(*m_scene, renderDevice, m_loader, m_defaultSpriteTexture);
#if HUSKY_DEVELOPMENT
                // 開発用の UI: UI のフレームを始めてから、渡された関数(tools の窓)に UI を作らせる。
                if (m_devUiCallback)
                {
                    renderDevice.BeginDevUiFrame();
                    m_devUiCallback(*this);
                }
#endif
                renderDevice.EndFrame();
                m_lastRenderStats = renderDevice.GetLastFrameStats();
            }
        }
    }
}
