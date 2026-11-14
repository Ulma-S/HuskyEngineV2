// Application.h の実装。
#include "engine/Application.h"
#include "engine/EngineLog.h"

#include <cmath> // std::round
#include <utility> // std::move

#include "core/Log.h"
#include "core/Profiler.h"

#include "core/MemoryTracker.h"
#include "engine/CameraComponent.h"
#include "engine/UIActor.h"
#include "engine/UIComponents.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Scene.h"
#include "engine/SceneAsset.h"
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
        RegisterEngineTypes(m_types); // engine の型(World、Actor、SpriteComponent など)を、名前から作れるようにする

        // シーンは、レジストリの中で一番最初に作るので、容量が極端に小さくない限り作れる。
        m_world = m_registry.Create<World>();
        m_registry.AddRoot(m_world->GetHandle());
        m_world->SetInput(&m_input); // アクターは GetWorld()->GetInput() で入力を読む
        m_world->SetAssetLoader(&m_loader); // アクターは GetWorld()->GetAssetLoader() でアセットの参照を引く
        m_loader.SetAudioDevice(&m_audio); // 読んだ音の波を音の層に預ける(出す先が無くても預けられる)
        // エンジンのアセットの種類の読み方を、ローダに登録する(AL-1〜3。新しい種類もここへ足す)。
        m_loader.RegisterKind(CreateTextureAssetKind());
        m_loader.RegisterKind(CreateSoundAssetKind());
        m_loader.RegisterKind(CreateFontAssetKind());
        m_loader.RegisterKind(CreateSpriteSheetAssetKind());
        m_loader.RegisterKind(CreatePrefabAssetKind());
        m_loader.RegisterKind(CreateScriptableDataAssetKind());
        m_loader.RegisterKind(CreateSceneAssetKind()); // SC-1
        m_world->SetAudio(&m_audio); // アクターやコンポーネントは GetWorld()->GetAudio() で音を鳴らす
    }

    double Application::StepFrame(std::uint64_t nowNanoseconds, const platform::InputEvents& events)
    {
        const double dt = BeginStepFrame(nowNanoseconds);
        // 2. 入力: このフレームのキーとマウスのイベントで、入力の状態を更新する
        m_input.BeginFrame(events);
        return FinishStepFrame(dt);
    }

    double Application::StepFrame(std::uint64_t nowNanoseconds, const std::vector<platform::KeyEvent>& keyEvents)
    {
        const double dt = BeginStepFrame(nowNanoseconds);
        m_input.BeginFrame(keyEvents); // 2. 入力(キーだけ)
        return FinishStepFrame(dt);
    }

    double Application::StepFrame(std::uint64_t nowNanoseconds, std::initializer_list<platform::KeyEvent> keyEvents)
    {
        m_listedKeyEvents.assign(keyEvents); // assign は確保したメモリを使い回す
        return StepFrame(nowNanoseconds, m_listedKeyEvents);
    }

    double Application::BeginStepFrame(std::uint64_t nowNanoseconds)
    {
        // 0. プロファイラーのフレームの区切り: 前のフレーム(その描画まで)に測った時間をまとめる
        core::Profiler::Get().EndFrame();
        core::Log::Get().AdvanceFrame(); // ログの記録のフレームの番号を進める

        // 1. このフレームの dt を決める(dt を決めるのは FrameClock だけ)
        return m_clock.Tick(nowNanoseconds);
    }

    double Application::FinishStepFrame(double dt)
    {
        // 2'. アセット: ワーカーが読み終わったものを、アセットに書く(GPU に送る)。ゲームの処理より前に行い、
        //     このフレームの更新と描画から、読み終わったアセットを使えるようにする。
        {
            HUSKY_PROFILE_SCOPE("Assets");
            m_loader.Update();
            m_audio.Update(); // 音のスレッドが手放した音の波を捨てる
        }

        // 2''. UI の入力(UI-6b): ボタンを押す・離す。ゲームのコード(更新コールバック、Tick)より先に行い、
        //      UI の上で押したマウスはゲームに見せない(Input::ConsumeMouse)。シーンを更新するフレームだけ
        //      (編集中のクリックはエディタが使う)。止めたら一度だけ、ボタンの見た目を戻す。
        if (m_sceneTickEnabled || m_sceneStepRequested)
        {
            m_uiEvents.Update(*m_world, m_input);
            m_uiEventsActive = true;
        }
        else if (m_uiEventsActive)
        {
            m_uiEvents.Reset(*m_world);
            m_uiEventsActive = false;
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
                HUSKY_PROFILE_SCOPE("World.Tick");
                m_world->Tick(dt);
            }
            // 当たり判定: アクターが動いた後に、コライダーを当たり判定の世界に伝え、重なりを調べてイベントを届ける。
            // 受け取った側は同じフレームのうちに反応でき、Destroy などはこの後のフレームの終わりで確定する。
            m_physics2D.Update(*m_world, dt); // 中で区間 "Physics2D.*" を測る
            m_sceneStepRequested = false;
        }

        // 3''. シーンを読む・下ろす(SC-2b)。Tick の途中ではなく、ここでまとめて行う。下ろしたアクターはこのフレームの終わりに消え、
        //      読んだアクターは次のフレームから更新される(UI の配置はこの後なので、このフレームから正しい場所に描かれる)。
        ProcessSceneRequests();

        // 3'. UI の配置(UI-5a): Canvas の下の UIActor の四角を求め、transform に書く。編集中(Tick が止まっている)も
        //     インスペクタで直した RectTransform が見えるように、毎フレーム行う。画面の大きさは入力と一緒に届いたもの。
        {
            HUSKY_PROFILE_SCOPE("UILayout");
            LayoutCanvases(*m_world, m_input.GetScreenWidth(), m_input.GetScreenHeight());
        }

        // 4. フレームの終わり: 破棄の確定と、必要なら GC。
        //    この順番が大事: Destroy 済みのアクターを一覧から外す → 破棄を確定する → (たまに)GC。
        HUSKY_PROFILE_SCOPE("FrameEnd"); // ここから関数の終わりまで
        m_world->RemoveDestroyedActors();
        m_registry.FlushDestroyed();
        m_secondsSinceGc += dt;
        if (m_secondsSinceGc >= GcIntervalSeconds)
        {
            m_collectedLastGc = m_registry.CollectGarbage();
            m_secondsSinceGc = 0.0;
            // 読むだけの ScriptableData が書き換えられていないか(開発ビルドだけ。製品では何もしない)。GC と同じ 1 秒に 1 回。
            CheckScriptableDataUnchanged(m_loader);
        }
        return dt;
    }

    const PrefabAsset* Application::FindPrefabForScene(const core::Guid& guid)
    {
        const PrefabAsset* prefab = m_loader.Resolve<PrefabAsset>(m_loader.Load<PrefabAsset>(guid));
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
        AddSceneData(file, path);
        return true;
    }

    void Application::AddSceneData(const ArchiveValue& data, const std::string& label, Scene* target)
    {
        const SceneSerializer::ReadReport report = SceneSerializer::Read(
            data, *m_world, m_types, [this](const core::Guid& guid) { return FindPrefabForScene(guid); });
        for (const std::string& warning : report.warnings)
        {
            HUSKY_LOG(SceneLog::ReadWarning, label, warning);
        }
        // 足したアクターをシーンへ移す。親のいないものだけを移せば、子孫は一緒に移る(親子は同じシーンの決まり)。
        if (target != nullptr && !target->IsRoot())
        {
            for (const core::ObjectHandle handle : report.addedActors)
            {
                Actor* actor = m_registry.Resolve<Actor>(handle);
                if (actor != nullptr && actor->GetParent() == nullptr)
                {
                    m_world->MoveToScene(*actor, *target);
                }
            }
        }
        HUSKY_LOG(SceneLog::Loaded, label, report.addedActorCount, report.createdCount, report.warnings.size());
    }

    namespace
    {
        // "scenes/title.scene" → "title"(ファイル名から拡張子を除く)。
        std::string SceneNameFromPath(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
            const std::size_t dot = path.find_last_of('.');
            const std::size_t end = dot == std::string::npos || dot < begin ? path.size() : dot;
            return path.substr(begin, end - begin);
        }
    }

    bool Application::LoadSceneList(const core::Guid& sceneListGuid)
    {
        m_sceneList.clear();
        core::AssetRef<SceneList> reference;
        reference.guid = sceneListGuid;
        const SceneList* list = m_loader.Resolve(reference);
        if (list == nullptr)
        {
            m_loader.WaitForAllLoads(); // 読み込み中なら待つ
            list = m_loader.Resolve(reference);
        }
        if (list == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneLoadFailed, sceneListGuid.ToString(), "シーンの一覧(SceneList の .sdata)を読めません");
            return false;
        }
        for (std::size_t i = 0; i < list->scenes.size(); ++i)
        {
            const core::Guid& guid = list->scenes[i].guid;
            const std::string* path = m_assets.FindPath(guid);
            if (guid.IsNull() || path == nullptr)
            {
                HUSKY_LOG(SceneLog::SceneListEntryMissing, static_cast<std::int64_t>(i));
                continue;
            }
            std::string sceneName = SceneNameFromPath(*path);
            if (FindSceneGuid(sceneName) != nullptr)
            {
                HUSKY_LOG(SceneLog::SceneListDuplicateName, sceneName, *path); // 黙ってどちらかを選ばない
                continue;
            }
            m_sceneList.push_back({std::move(sceneName), guid});
        }
        return true;
    }

    const core::Guid* Application::FindSceneGuid(std::string_view sceneName) const
    {
        for (const SceneListEntry& entry : m_sceneList)
        {
            if (entry.name == sceneName)
            {
                return &entry.guid;
            }
        }
        return nullptr;
    }

    const std::string& Application::GetFirstSceneName() const
    {
        static const std::string empty;
        return m_sceneList.empty() ? empty : m_sceneList.front().name;
    }

    void Application::ProcessSceneRequests()
    {
        if (m_world->GetSceneRequests().empty())
        {
            return; // ふつうのフレームはここで終わる(確保しない)
        }
        // 頼まれたものを写してから空にする(読んでいる間に頼まれたものは、次のフレームに回す)。
        const std::vector<World::SceneRequest> requests = m_world->GetSceneRequests();
        m_world->ClearSceneRequests();
        for (const World::SceneRequest& request : requests)
        {
            Scene* existing = m_world->FindScene(request.sceneName);
            if (!request.load)
            {
                if (existing == nullptr)
                {
                    HUSKY_LOG(SceneLog::SceneNotLoaded, request.sceneName);
                }
                else if (existing->IsRoot())
                {
                    HUSKY_LOG(SceneLog::RootSceneUnload, request.sceneName);
                }
                else
                {
                    std::size_t destroyed = 0;
                    m_world->UnloadSceneNow(*existing, &destroyed);
                    HUSKY_LOG(SceneLog::SceneUnloaded, request.sceneName, static_cast<std::int64_t>(destroyed));
                }
                continue;
            }

            if (existing != nullptr)
            {
                HUSKY_LOG(SceneLog::SceneAlreadyLoaded, request.sceneName);
                continue;
            }
            const core::Guid* guid = FindSceneGuid(request.sceneName);
            if (guid == nullptr)
            {
                HUSKY_LOG(SceneLog::SceneNotInList, request.sceneName);
                continue;
            }
            Scene* parent = m_world->GetRootScene();
            if (!request.parent.IsNull())
            {
                parent = m_registry.Resolve<Scene>(request.parent);
                if (parent == nullptr || parent->IsPendingDestroy())
                {
                    HUSKY_LOG(SceneLog::SceneLoadFailed, request.sceneName, "親のシーンがもうありません");
                    continue;
                }
            }
            if (parent != nullptr)
            {
                LoadChildSceneNow(*guid, *parent);
            }
        }
    }

    namespace
    {
        // シーンのアセットを読み終わるまで待って返す。読めなければ nullptr。
        const SceneAsset* LoadSceneAssetNow(AssetLoader& loader, const core::Guid& guid)
        {
            core::AssetRef<SceneAsset> reference;
            reference.guid = guid;
            const SceneAsset* asset = loader.Resolve(reference);
            if (asset != nullptr && asset->GetState() == AssetState::Loading)
            {
                loader.WaitForAllLoads(); // 途中まで読めたフレームを作らない
            }
            return asset != nullptr && asset->IsLoaded() ? asset : nullptr;
        }

        // シーンの中を探す(GUID で。同じシーンは同時に1つだけの決まりを、名前と GUID の両方で確かめる)。
        bool IsSceneAssetLoaded(core::ObjectRegistry& registry, const Scene& scene, const core::Guid& guid, int depth)
        {
            if (scene.asset == guid)
            {
                return true;
            }
            for (const core::ObjectHandle handle : scene.children)
            {
                const Scene* child = registry.Resolve<Scene>(handle);
                if (child != nullptr && !child->IsPendingDestroy() && depth < 64
                    && IsSceneAssetLoaded(registry, *child, guid, depth + 1))
                {
                    return true;
                }
            }
            return false;
        }
    }

    Scene* Application::LoadChildSceneNow(const core::Guid& guid, Scene& parent)
    {
        const std::string* path = m_assets.FindPath(guid);
        if (path == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneLoadFailed, guid.ToString(), "アセットの表にありません");
            return nullptr;
        }
        const std::string sceneName = SceneNameFromPath(*path);
        if (m_world->FindScene(sceneName) != nullptr || IsSceneAssetLoaded(m_registry, *m_world->GetRootScene(), guid, 0))
        {
            HUSKY_LOG(SceneLog::SceneAlreadyLoaded, sceneName); // 輪(自分を中に含む)もここで止まる
            return nullptr;
        }
        const SceneAsset* asset = LoadSceneAssetNow(m_loader, guid);
        if (asset == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneLoadFailed, sceneName, "シーンのファイルを読めません");
            return nullptr;
        }
        Scene* scene = m_world->CreateChildScene(parent, sceneName, guid);
        if (scene == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneLoadFailed, sceneName, "シーンを作れません(メモリが足りません)");
            return nullptr;
        }
        std::vector<std::string> warnings;
        SceneSerializer::ReadChildScenes(asset->GetData(), scene->childScenes, &warnings);
        for (const std::string& warning : warnings)
        {
            HUSKY_LOG(SceneLog::ReadWarning, sceneName, warning);
        }
        AddSceneData(asset->GetData(), sceneName, scene);
        LoadMarkedChildScenes(*scene);
        return scene;
    }

    void Application::LoadMarkedChildScenes(Scene& scene)
    {
        // 読んでいる間に scene.childScenes は変わらないが、写してから回す(読む先で何が起きても壊れないように)。
        const std::vector<Scene::ChildSceneEntry> entries = scene.childScenes;
        for (const Scene::ChildSceneEntry& entry : entries)
        {
            if (entry.loadOnOpen)
            {
                LoadChildSceneNow(entry.scene, scene);
            }
        }
    }

    void Application::ClearWorld()
    {
        for (const core::ObjectHandle handle : m_world->actors)
        {
            if (Actor* actor = m_registry.Resolve<Actor>(handle))
            {
                actor->Destroy();
            }
        }
        m_world->UnloadAllChildScenes();
        m_world->RemoveDestroyedActors(); // 一覧から外す(メモリから消えるのはフレームの終わりの破棄の確定)
        if (Scene* root = m_world->GetRootScene())
        {
            root->name.clear();
            root->asset = core::Guid();
            root->childScenes.clear();
        }
    }

    void Application::OpenRootSceneData(const ArchiveValue& data, std::string_view sceneName, const core::Guid& asset,
                                        const std::string& label, bool loadChildren)
    {
        ClearWorld();
        Scene* root = m_world->GetRootScene();
        if (root == nullptr)
        {
            return;
        }
        root->name = std::string(sceneName);
        root->asset = asset;
        std::vector<std::string> warnings;
        SceneSerializer::ReadChildScenes(data, root->childScenes, &warnings);
        for (const std::string& warning : warnings)
        {
            HUSKY_LOG(SceneLog::ReadWarning, label, warning);
        }
        AddSceneData(data, label); // 指定が無いのでルートシーンに入る
        if (loadChildren)
        {
            LoadMarkedChildScenes(*root);
        }
    }

    bool Application::OpenRootScene(std::string_view sceneName, bool loadChildren)
    {
        const core::Guid* guid = FindSceneGuid(sceneName);
        if (guid == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneNotInList, std::string(sceneName));
            return false;
        }
        const core::Guid found = *guid; // ClearWorld の後も使うので写す
        const SceneAsset* asset = LoadSceneAssetNow(m_loader, found);
        if (asset == nullptr)
        {
            HUSKY_LOG(SceneLog::SceneLoadFailed, std::string(sceneName), "シーンのファイルを読めません");
            return false;
        }
        // 中身を写してから開く(開く途中の GC でアセットが回収されても壊れないように)。
        const ArchiveValue data = asset->GetData();
        OpenRootSceneData(data, sceneName, found, std::string(sceneName), loadChildren);
        return true;
    }

    bool Application::OpenRootSceneFile(const std::string& path, bool loadChildren)
    {
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
        OpenRootSceneData(file, SceneNameFromPath(path), core::Guid(), path, loadChildren);
        return true;
    }

    ArchiveValue Application::CaptureScenes(std::vector<std::string>* warnings)
    {
        ArchiveValue snapshot = ArchiveValue::MakeObject();
        ArchiveValue& entries = snapshot.Set("scenes", ArchiveValue::MakeArray());
        // 親が先になる順(深さ優先)に並べる。番号は entries の中の位置。
        struct Pending
        {
            Scene* scene;
            std::int64_t parent;
        };
        std::vector<Pending> stack{{m_world->GetRootScene(), -1}};
        std::int64_t index = 0;
        while (!stack.empty())
        {
            const Pending current = stack.back();
            stack.pop_back();
            if (current.scene == nullptr)
            {
                continue;
            }
            SceneSerializer::WriteResult written = SceneSerializer::WriteScene(*m_world, *current.scene);
            if (warnings != nullptr)
            {
                warnings->insert(warnings->end(), written.warnings.begin(), written.warnings.end());
            }
            ArchiveValue entry = ArchiveValue::MakeObject();
            entry.Set("name", ArchiveValue(current.scene->name));
            entry.Set("asset", ArchiveValue(current.scene->asset.ToString()));
            entry.Set("parent", ArchiveValue(current.parent));
            entry.Set("data", std::move(written.value));
            entries.Append(std::move(entry));
            const std::int64_t self = index++;
            // 子は逆の順に積む(取り出すときに元の順になる)
            for (auto it = current.scene->children.rbegin(); it != current.scene->children.rend(); ++it)
            {
                Scene* child = m_registry.Resolve<Scene>(*it);
                if (child != nullptr && !child->IsPendingDestroy())
                {
                    stack.push_back({child, self});
                }
            }
        }
        return snapshot;
    }

    std::size_t Application::RestoreScenes(const ArchiveValue& snapshot, std::vector<std::string>* warnings)
    {
        ClearWorld();
        const ArchiveValue* entries = snapshot.Find("scenes");
        if (entries == nullptr)
        {
            return 0;
        }
        const std::size_t before = m_world->actors.size();
        std::vector<Scene*> created; // 控えの番号 → 作ったシーン
        for (std::size_t i = 0; i < entries->GetArraySize(); ++i)
        {
            const ArchiveValue& entry = entries->At(i);
            const ArchiveValue* data = entry.Find("data");
            const ArchiveValue* nameValue = entry.Find("name");
            const ArchiveValue* assetValue = entry.Find("asset");
            const ArchiveValue* parentValue = entry.Find("parent");
            const std::string sceneName = nameValue != nullptr ? nameValue->AsString() : std::string();
            core::Guid asset;
            if (assetValue != nullptr)
            {
                core::Guid::Parse(assetValue->AsString(), asset);
            }
            const std::int64_t parentIndex = parentValue != nullptr ? parentValue->AsInt(-1) : -1;
            Scene* scene = nullptr;
            if (i == 0)
            {
                scene = m_world->GetRootScene();
                if (scene != nullptr)
                {
                    scene->name = sceneName;
                    scene->asset = asset;
                }
            }
            else if (parentIndex >= 0 && static_cast<std::size_t>(parentIndex) < created.size() && created[parentIndex] != nullptr)
            {
                scene = m_world->CreateChildScene(*created[parentIndex], sceneName, asset);
            }
            created.push_back(scene);
            if (scene == nullptr || data == nullptr)
            {
                if (warnings != nullptr)
                {
                    warnings->push_back("控えの " + std::to_string(i) + " 番目のシーンを作れません");
                }
                continue;
            }
            SceneSerializer::ReadChildScenes(*data, scene->childScenes, warnings);
            const SceneSerializer::ReadReport report = SceneSerializer::Read(
                *data, *m_world, m_types, [this](const core::Guid& guid) { return FindPrefabForScene(guid); });
            if (warnings != nullptr)
            {
                warnings->insert(warnings->end(), report.warnings.begin(), report.warnings.end());
            }
            if (!scene->IsRoot())
            {
                for (const core::ObjectHandle handle : report.addedActors)
                {
                    Actor* actor = m_registry.Resolve<Actor>(handle);
                    if (actor != nullptr && actor->GetParent() == nullptr)
                    {
                        m_world->MoveToScene(*actor, *scene);
                    }
                }
            }
        }
        return m_world->actors.size() - before;
    }

#if HUSKY_DEVELOPMENT
    bool Application::SaveSceneFile(const std::string& path)
    {
        // オブジェクト → 値の木 → 文字列 → ファイル、の順に変換する(LoadSceneFile の逆)。
        // 書くのはルートシーン(開いているシーン)に直接いるアクターと、子のシーンの記録だけ(SC-3)。
        Scene* root = m_world->GetRootScene();
        if (root == nullptr)
        {
            return false;
        }
        const SceneSerializer::WriteResult saved = SceneSerializer::WriteScene(*m_world, *root);
        if (saved.outsideReferences > 0)
        {
            // シーンをまたぐ参照は保存できない決まり(ユーザーの決定)。null と書いて保存は続ける。
            HUSKY_LOG(SceneLog::CrossSceneReference, path, static_cast<std::int64_t>(saved.outsideReferences));
        }
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
        HUSKY_LOG(SceneLog::Saved, path, m_world->actors.size(), text.size());
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
        m_defaultSpriteTexture = m_loader.Load<TextureAsset>(*guid);
        // ローダが返すハンドルは弱い参照なので、根に加えて、GC に回収されないようにする。
        m_registry.AddRoot(m_defaultSpriteTexture);
    }

    void Application::RunLoop(render::RenderDevice& renderDevice)
    {
        // 入力のイベントの受け皿。ループの外に置いて毎フレーム使い回し、メモリの確保を減らす。
        platform::InputEvents inputEvents;

        // FPS 表示用の集計(1秒ごとにリセットする)
        int framesThisSecond = 0;
        double secondsThisSecond = 0.0;
        // 前回のログの時点での、起動からの確保の回数。差を取って「この1秒に何回確保したか」を出す。
        std::uint64_t totalAllocationsAtLastLog = core::GetMemoryStats().totalAllocations;

        // ゲームループ。1周 = 1フレーム。
        while (true)
        {
            // OS のイベントを処理する。閉じるボタンなら終わる。
            if (platform::ProcessEvents(inputEvents))
            {
                break;
            }
            // 画面の大きさも入力と一緒に渡す(マウスの位置をワールドへ直すのに使う)。窓を広げると変わるので毎フレーム。
            inputEvents.screenWidth = static_cast<float>(renderDevice.GetScreenWidth());
            inputEvents.screenHeight = static_cast<float>(renderDevice.GetScreenHeight());

            // 描画以外の1フレーム分(dt、入力、更新、フレームの終わり)
            const double dt = StepFrame(platform::GetTimeNanoseconds(), inputEvents);
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
                CameraMatrices camera = MakeSceneCamera(*m_world,
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
                    DrawSprites(*m_world, renderDevice, m_loader, m_defaultSpriteTexture);
#if HUSKY_DEVELOPMENT
                    if (m_afterSceneDraw) // エディタの移動のギズモなど、シーンより手前に描くもの
                    {
                        m_afterSceneDraw(renderDevice);
                    }
#endif
                    // 画面のパス(UI-2a): 行列を画面ちょうどのカメラ(左下が原点、1 単位 = 1 ピクセル)に替えてから積む。
                    // 替えた後に積んだものだけが新しい行列で描かれ、シーンはワールドのカメラのまま。積む順なので、シーンより手前。
                    // ゲームのカメラが動いても、エディタのカメラで見ていても、画面の同じ所に出る(後のゲーム用 UI の Canvas)。
                    // Canvas の下の UIActor(UI-5b)は、ここで描く(ワールドの行列がそのまま画面のピクセルになっている)。
                    renderDevice.SetViewProjection(MakeScreenCamera(static_cast<float>(renderDevice.GetScreenWidth()),
                                                                    static_cast<float>(renderDevice.GetScreenHeight()))
                                                       .ViewProjection());
                    DrawSprites(*m_world, renderDevice, m_loader, m_defaultSpriteTexture, SpritePass::Screen);
                    DrawUI(*m_world, renderDevice, m_loader, m_uiTextLayout); // UI の画像と文字(UI-6a)
                    if (m_screenDraw)
                    {
                        m_screenDraw(renderDevice);
                    }
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
