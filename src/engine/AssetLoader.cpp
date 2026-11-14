// AssetLoader.h と、Asset.h の箱の土台、PrefabAsset.h の実装。ScriptableData.h の箱(ScriptableDataAsset)のデストラクタも。
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"

#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"
#include "engine/Prefab.h"          // 入れ子のプレハブを開く(Prefab::ExpandNested。HIER)
#include "engine/SceneSerializer.h"
#include "platform/FileSystem.h"
#include "platform/Image.h"


#include <algorithm> // std::find
#include <utility> // std::move, std::swap

namespace husky::engine
{
    namespace
    {
        // ---- プレハブと ScriptableData(ジョブの中で呼ぶ) ----

        // JSON のファイル(.prefab、.sdata)を読んで、値の木にする。形(プレハブならアクターが1つか)は、使う側が確かめる。
        bool ReadJsonFile(const std::string& path, ArchiveValue& outData, std::vector<std::string>& outWarnings)
        {
            std::string text;
            std::string error;
            if (!platform::ReadTextFile(path.c_str(), text))
            {
                return false; // 理由は platform がログに出す
            }
            if (!JsonFormat::Read(text, outData, &error))
            {
                outWarnings.push_back("JSON として読めません: " + error);
                return false;
            }
            return true;
        }

        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }
    }

    Asset::~Asset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnAssetDestroyed(*this);
        }
    }

    void Asset::ReleaseLoadedData()
    {
        if (m_loader != nullptr && !m_released)
        {
            m_released = true;
            m_loader->ReleaseAssetData(*this);
        }
    }

    PrefabAsset::~PrefabAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnPrefabDestroyed(*this);
        }
    }

    ScriptableDataAsset::~ScriptableDataAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnScriptableDataDestroyed(*this);
        }
    }

    AssetLoader::AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry, core::JobSystem& jobs,
                             const core::TypeRegistry& types)
        : m_database(database)
        , m_registry(registry)
        , m_types(types)
        , m_jobs(jobs)
    {
        // registry は、ここではまだ使わない(Application では、このローダより後に作られるため。
        // 参照を覚えておくだけなら、相手が作られる前でもよい)。
    }

    AssetLoader::~AssetLoader()
    {
        // ジョブは記録(m_pending の中身)に書き込むので、記録を壊す前に、すべて終わるのを待つ。
        for (const std::unique_ptr<PendingLoad>& load : m_pending)
        {
            m_jobs.Wait(load->counter);
        }
        m_pending.clear(); // 結果は捨てる(レジストリはもう無いことがあるので、アセットには触らない)
    }

    void AssetLoader::SetRenderDevice(render::RenderDevice* device)
    {
        if (device == m_device)
        {
            return;
        }

        // 前の GPU で作ったものを、種類ごとに手放させる(GPU が外れる、または別の GPU に替わるため)。
        if (m_device != nullptr)
        {
            for (const KindEntry& entry : m_kinds)
            {
                for (const auto& cached : entry.cache) // cached.first が GUID、cached.second がハンドル
                {
                    if (Asset* asset = m_registry.Resolve<Asset>(cached.second))
                    {
                        entry.kind->ReleaseRenderResources(*this, *asset, *m_device);
                    }
                }
            }
        }
        m_device = device;

        // 新しい GPU のために、読み終わっていて、種類が「読み直しが要る」と答えた箱を、読み直す。
        // (読み込み中のものは、終わったとき(Finish)に、その時点の GPU に送られるので、読み直さない)
        // StartKindLoad はキャッシュを書き換えないので、回しながら呼んでよい。
        if (m_device != nullptr)
        {
            for (const KindEntry& entry : m_kinds)
            {
                for (const auto& cached : entry.cache)
                {
                    Asset* asset = m_registry.Resolve<Asset>(cached.second);
                    if (asset != nullptr && asset->IsLoaded() && entry.kind->NeedsReloadForRenderDevice(*asset))
                    {
                        StartKindLoad(*asset);
                    }
                }
            }
        }
    }

    void AssetLoader::Update()
    {
        // ワーカーがいなければ、読み込みのジョブは誰も実行しないので、ここで実行する(同期実行)。
        if (m_jobs.GetWorkerCount() == 0)
        {
            m_jobs.RunPendingJobs();
        }

        // 終わった記録の結果をアセットに書き、記録を外す。
        // 外すときは、最後の記録と入れ替えてから末尾を消す(途中を消して後ろを詰めるより速い。順番は気にしない)。
        // Finish の中で新しい読み込みが始まる(m_pending に足される)ことがあるので、要素は毎回 m_pending から引く。
        for (std::size_t i = 0; i < m_pending.size();)
        {
            if (!m_pending[i]->counter.IsDone())
            {
                ++i;
                continue;
            }
            std::unique_ptr<PendingLoad> done = std::move(m_pending[i]);
            std::swap(m_pending[i], m_pending.back());
            m_pending.pop_back();
            done->Finish(*this); // 外してから書く(Finish の中で m_pending が増えても、done は動かない)
        }
        UpdateWaitingPrefabs();
    }

    bool AssetLoader::WaitsOn(const core::Guid& from, const core::Guid& target, int depth) const
    {
        if (depth > 32)
        {
            return true; // 深すぎる: 輪とみなす
        }
        for (const WaitingPrefab& waiting : m_waitingPrefabs)
        {
            const PrefabAsset* asset = ResolvePrefab(waiting.asset);
            if (asset == nullptr || asset->GetGuid() != from)
            {
                continue;
            }
            for (const core::Guid& nested : waiting.nested)
            {
                if (nested == target || WaitsOn(nested, target, depth + 1))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void AssetLoader::DropWaiting(std::size_t index)
    {
        for (const core::ObjectHandle nested : m_waitingPrefabs[index].nestedAssets)
        {
            m_registry.RemoveRoot(nested);
        }
        std::swap(m_waitingPrefabs[index], m_waitingPrefabs.back());
        m_waitingPrefabs.pop_back();
    }

    bool AssetLoader::UpdateWaitingPrefabs()
    {
        bool progressed = false;
        for (std::size_t i = 0; i < m_waitingPrefabs.size();)
        {
            PrefabAsset* asset = ResolvePrefab(m_waitingPrefabs[i].asset);
            if (asset == nullptr || asset->m_state != AssetState::Loading)
            {
                DropWaiting(i); // 回収された・読み直しで外れた
                progressed = true;
                continue;
            }
            bool ready = true;
            bool loop = false;
            for (const core::ObjectHandle nestedHandle : m_waitingPrefabs[i].nestedAssets)
            {
                const PrefabAsset* nested = ResolvePrefab(nestedHandle);
                if (nested != nullptr && nested->GetState() == AssetState::Loading)
                {
                    ready = false;
                    loop = loop || nested->GetGuid() == asset->m_guid || WaitsOn(nested->GetGuid(), asset->m_guid, 0);
                }
            }
            if (loop)
            {
                HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", asset->m_guid.ToString(),
                          "入れ子のプレハブが輪になっている(自分を中に含む)ので読めません");
                asset->m_state = AssetState::Failed;
                DropWaiting(i);
                progressed = true;
                continue;
            }
            if (!ready)
            {
                ++i;
                continue;
            }
            // 入れ子が読み終わった(読めなかったものは、ファイルにある値だけで作る): 開いた形を作る
            std::vector<std::string> warnings;
            const SceneSerializer::PrefabFinder finder = [this](const core::Guid& guid) -> const PrefabAsset* {
                const auto found = m_prefabs.find(guid);
                const PrefabAsset* nested = found != m_prefabs.end() ? ResolvePrefab(found->second) : nullptr;
                return nested != nullptr && nested->IsLoaded() ? nested : nullptr;
            };
            asset->m_data = Prefab::ExpandNested(asset->m_raw, m_registry, m_types, finder, warnings);
            for (const std::string& warning : warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", asset->m_guid.ToString(), warning);
            }
            asset->m_state = AssetState::Loaded;
            DropWaiting(i);
            progressed = true;
        }
        return progressed;
    }

    void AssetLoader::WaitForAllLoads()
    {
        // スプライトシートが読み終わると、中の画像の読み込みが新しく始まる。増えたものも含めて、無くなるまで繰り返す。
        while (!m_pending.empty() || !m_waitingPrefabs.empty())
        {
            for (const std::unique_ptr<PendingLoad>& load : m_pending)
            {
                m_jobs.Wait(load->counter); // 待っている間、このスレッドも積まれたジョブを実行する
            }
            const bool hadPending = !m_pending.empty();
            Update();
            // 読み込み中のものが無いのに、入れ子を待っているプレハブが進まない: 待つ相手がもう来ない(念のため、読めなかったことにする)
            if (!hadPending && m_pending.empty() && !m_waitingPrefabs.empty() && !UpdateWaitingPrefabs())
            {
                while (!m_waitingPrefabs.empty())
                {
                    if (PrefabAsset* stuck = ResolvePrefab(m_waitingPrefabs.back().asset))
                    {
                        stuck->m_state = AssetState::Failed;
                    }
                    DropWaiting(m_waitingPrefabs.size() - 1);
                }
            }
        }
    }

    bool AssetLoader::CanLoadAs(const core::TypeInfo& assetType, std::string_view relativePath)
    {
        // アセットの型ごとに、読めるファイルの拡張子を決める。新しい型を足したら、ここにも足す。
        if (assetType.IsA(TextureAsset::StaticType()))
        {
            return EndsWith(relativePath, ".png"); // 1段目(TextureAsset.cpp)が PNG を読むため
        }
        if (assetType.IsA(FontAsset::StaticType()))
        {
            return EndsWith(relativePath, ".ttf") || EndsWith(relativePath, ".otf");
        }
        if (assetType.IsA(SoundAsset::StaticType()))
        {
            return EndsWith(relativePath, ".wav"); // 読めるのは WAV だけ(audio::DecodeWav)
        }
        if (assetType.IsA(SpriteSheetAsset::StaticType()))
        {
            return EndsWith(relativePath, ".spritesheet");
        }
        if (assetType.IsA(PrefabAsset::StaticType()))
        {
            return EndsWith(relativePath, ".prefab");
        }
        // AssetRef<BouncerSettings> のアセットの型は、中身の型(ScriptableData の子)。箱の型で聞かれても同じに答える。
        // ファイルの中の "type" までは見ない(どの子の型かで絞るのは、要るようになったら)。
        if (assetType.IsA(ScriptableData::StaticType()) || assetType.IsA(ScriptableDataAsset::StaticType()))
        {
            return EndsWith(relativePath, ".sdata");
        }
        return false;
    }

    std::string AssetLoader::FindPathOrLog(const core::Guid& guid) const
    {
        std::string path = m_database.GetFullPath(guid);
        if (path.empty())
        {
            HUSKY_LOG(AssetLog::GuidNotInDatabase, guid.ToString());
        }
        return path;
    }

    template <typename Record, typename Job>
    void AssetLoader::Schedule(std::unique_ptr<Record> record, Job&& job)
    {
        Record* raw = record.get(); // ジョブには、記録の場所(ポインタ1つ)だけを渡す
        core::JobCounter& counter = raw->counter;
        m_pending.push_back(std::move(record));
        m_jobs.Schedule([raw, job] { job(*raw); }, counter);
    }

    // ================= 登録した種類の共通の流れ(AL-1) =================

    void AssetLoader::RegisterKind(std::unique_ptr<AssetKind> kind)
    {
        if (kind == nullptr)
        {
            return;
        }
        const core::TypeInfo& type = kind->GetAssetType();
        for (const KindEntry& entry : m_kinds)
        {
            if (&entry.kind->GetAssetType() == &type)
            {
                HUSKY_LOG(AssetLog::KindAlreadyRegistered, type.GetName());
                return;
            }
        }
        KindEntry entry;
        entry.kind = std::move(kind);
        m_kinds.push_back(std::move(entry));
    }

    AssetLoader::KindEntry* AssetLoader::FindKindEntry(const core::TypeInfo& assetType)
    {
        for (KindEntry& entry : m_kinds)
        {
            if (assetType.IsA(entry.kind->GetAssetType()))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const AssetLoader::KindEntry* AssetLoader::FindKindEntry(const core::TypeInfo& assetType) const
    {
        return const_cast<AssetLoader*>(this)->FindKindEntry(assetType); // 中身は同じ(書き換えない)
    }

    const AssetKind* AssetLoader::FindKind(const core::TypeInfo& assetType) const
    {
        const KindEntry* entry = FindKindEntry(assetType);
        return entry != nullptr ? entry->kind.get() : nullptr;
    }

    std::size_t AssetLoader::GetLoadedCount(const core::TypeInfo& assetType) const
    {
        const KindEntry* entry = FindKindEntry(assetType);
        return entry != nullptr ? entry->cache.size() : 0;
    }

    core::ObjectHandle AssetLoader::Load(const core::TypeInfo& assetType, const core::Guid& guid)
    {
        KindEntry* entry = FindKindEntry(assetType);
        if (entry == nullptr)
        {
            HUSKY_LOG(AssetLog::KindNotRegistered, assetType.GetName(), guid.ToString());
            return {};
        }

        // 1. キャッシュ: まだ生きていれば、それを返す(読み込み中でも)。破棄を予約済み(この後で消える)なら、作り直す。
        const auto found = entry->cache.find(guid);
        if (found != entry->cache.end())
        {
            const Asset* cached = m_registry.Resolve<Asset>(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        // 2. 箱を「読み込み中」で作り、ジョブを積む。
        Asset* asset = entry->kind->CreateAsset(m_registry);
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, entry->kind->GetDisplayName(), guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        asset->m_kind = entry->kind.get();
        StartKindLoad(*asset);
        entry->cache[guid] = asset->GetHandle(); // 古いもの(破棄を予約済み)があれば置き換える
        return asset->GetHandle();
    }

    Asset* AssetLoader::ResolveReference(core::AssetReference& reference, const core::TypeInfo& assetType)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        // 前に読んだ箱が、まだ生きていて、同じ GUID で、型が合うなら、それを使う(毎フレーム呼ばれるので、ここは速く済ませる)。
        Asset* asset = m_registry.Resolve<Asset>(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid && asset->GetType().IsA(assetType))
        {
            return asset;
        }
        reference.asset = Load(assetType, reference.guid);
        asset = m_registry.Resolve<Asset>(reference.asset);
        return (asset != nullptr && asset->GetType().IsA(assetType)) ? asset : nullptr;
    }

    void AssetLoader::StartKindLoad(Asset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid); // 表はゲームスレッドでだけ引く
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto pending = std::make_unique<PendingKindLoad>();
        pending->asset = asset.GetHandle();
        pending->guid = asset.m_guid;
        pending->path = path;
        pending->kind = asset.m_kind;
        pending->load = asset.m_kind->CreateLoad(asset);
        pending->load->guid = asset.m_guid;
        pending->load->path = std::move(path);
        // ジョブ(ワーカースレッド): 種類の記録の 1段目と 2段目だけを動かす。記録の外は触らない。
        Schedule(std::move(pending), [](PendingKindLoad& record) {
            AssetLoad& load = *record.load;
            load.succeeded = load.ReadSource() && load.Cook();
        });
    }

    void AssetLoader::FinishKindLoad(PendingKindLoad& pending)
    {
        // 書く先の箱が、まだ生きていて、読み込み中のままか(途中で回収された、作り直された、なら捨てる)。
        Asset* asset = m_registry.Resolve<Asset>(pending.asset);
        if (asset == nullptr || asset->m_guid != pending.guid || asset->m_kind != pending.kind
            || asset->m_state != AssetState::Loading)
        {
            return;
        }
        asset->m_state = pending.kind->Finish(*this, *asset, *pending.load) ? AssetState::Loaded : AssetState::Failed;
    }

    void AssetLoader::ReleaseAssetData(Asset& asset)
    {
        if (asset.m_kind != nullptr)
        {
            asset.m_kind->Release(*this, asset);
        }
    }

    void AssetLoader::OnAssetDestroyed(Asset& asset)
    {
        if (asset.m_kind == nullptr)
        {
            return;
        }
        KindEntry* entry = FindKindEntry(asset.m_kind->GetAssetType());
        if (entry == nullptr)
        {
            return;
        }
        // キャッシュが、この箱を指しているときだけ外す(同じ GUID で、もう新しいものを作っていることがある)。
        // 読み込み中の記録は、ここでは消さない(ジョブがまだ書き込んでいるかもしれない)。終わったときに捨てる。
        const auto found = entry->cache.find(asset.m_guid);
        if (found != entry->cache.end() && found->second == asset.GetHandle())
        {
            entry->cache.erase(found);
        }
    }

    // ================= プレハブ =================
    // 流れはスプライトシートと同じ(キャッシュを見る → 無ければ作って読み始める → ジョブ → Finish で値を移す)。

    core::ObjectHandle AssetLoader::LoadPrefab(const core::Guid& guid)
    {
        const auto found = m_prefabs.find(guid);
        if (found != m_prefabs.end())
        {
            const PrefabAsset* cached = ResolvePrefab(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        PrefabAsset* asset = m_registry.Create<PrefabAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "プレハブ", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartPrefabLoad(*asset);
        m_prefabs[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    PrefabAsset* AssetLoader::ResolvePrefab(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<PrefabAsset>(handle);
    }

    PrefabAsset* AssetLoader::Resolve(core::AssetRef<PrefabAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        PrefabAsset* asset = ResolvePrefab(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadPrefab(reference.guid);
        return ResolvePrefab(reference.asset);
    }

    void AssetLoader::StartPrefabLoad(PrefabAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingPrefabLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読み、記録の data、warnings、succeeded に書く。
        // 値の木はただのデータ(GC 管理のオブジェクトではない)なので、ジョブで作ってよい。
        Schedule(std::move(record), [](PendingPrefabLoad& load) {
            load.succeeded = ReadJsonFile(load.path, load.data, load.warnings);
        });
    }

    void AssetLoader::FinishPrefab(PendingPrefabLoad& load)
    {
        for (const std::string& warning : load.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", load.path, warning);
        }
        PrefabAsset* asset = ResolvePrefab(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_raw = std::move(load.data); // 値の木を、コピーせずにアセットへ移す
        // 入れ子のプレハブ(別のプレハブから置いたもの)が無ければ、そのまま使える
        const std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(asset->m_raw);
        if (nested.empty())
        {
            asset->m_data = asset->m_raw;
            asset->m_state = AssetState::Loaded;
            return;
        }
        // 入れ子があれば、先にそれを読む(読み終わってから開いた形を作る。UpdateWaitingPrefabs)
        WaitingPrefab waiting;
        waiting.asset = asset->GetHandle();
        waiting.nested = nested;
        for (const core::Guid& guid : nested)
        {
            const core::ObjectHandle handle = LoadPrefab(guid);
            m_registry.AddRoot(handle); // 待っている間に回収されないように
            waiting.nestedAssets.push_back(handle);
        }
        m_waitingPrefabs.push_back(std::move(waiting));
    }

    void AssetLoader::OnPrefabDestroyed(PrefabAsset& asset)
    {
        const auto found = m_prefabs.find(asset.m_guid);
        if (found != m_prefabs.end() && found->second == asset.GetHandle())
        {
            m_prefabs.erase(found);
        }
    }

    std::size_t AssetLoader::ReloadPrefabsNesting(const core::Guid& guid)
    {
        // 先に集めてから読み直す(読み直すと m_prefabs が変わるため)
        std::vector<core::Guid> outers;
        for (const auto& [cachedGuid, handle] : m_prefabs)
        {
            const PrefabAsset* cached = ResolvePrefab(handle);
            if (cached == nullptr || cachedGuid == guid)
            {
                continue;
            }
            const std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(cached->GetRawData());
            if (std::find(nested.begin(), nested.end(), guid) != nested.end())
            {
                outers.push_back(cachedGuid);
            }
        }
        std::size_t count = 0;
        for (const core::Guid& outer : outers)
        {
            ReloadPrefab(outer);
            count += 1 + ReloadPrefabsNesting(outer); // その外側も(輪は読み込みで断るので、ここでは深さを気にしない)
        }
        return count;
    }

    core::ObjectHandle AssetLoader::ReloadPrefab(const core::Guid& guid)
    {
        m_prefabs.erase(guid); // キャッシュから外すだけ(古いアセットの後始末は OnPrefabDestroyed。外した後は何もしない)
        return LoadPrefab(guid);
    }

    // ================= ScriptableData =================
    // 流れはプレハブと同じ。違うのは Finish で、値の木の "type" の名前から中身を作り、Property 越しに値を入れること。

    core::ObjectHandle AssetLoader::LoadScriptableData(const core::Guid& guid)
    {
        const auto found = m_scriptableData.find(guid);
        if (found != m_scriptableData.end())
        {
            const ScriptableDataAsset* cached = ResolveScriptableDataAsset(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        ScriptableDataAsset* asset = m_registry.Create<ScriptableDataAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "ScriptableData", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartScriptableDataLoad(*asset);
        m_scriptableData[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    ScriptableDataAsset* AssetLoader::ResolveScriptableDataAsset(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<ScriptableDataAsset>(handle);
    }

    const ScriptableData* AssetLoader::GetScriptableData(const ScriptableDataAsset& asset) const
    {
        if (!asset.IsLoaded())
        {
            return nullptr;
        }
        return m_registry.Resolve<ScriptableData>(asset.m_data);
    }

    const ScriptableData* AssetLoader::ResolveScriptableData(core::AssetReference& reference, const core::TypeInfo& expected)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        ScriptableDataAsset* asset = ResolveScriptableDataAsset(reference.asset);
        if (asset == nullptr || asset->GetGuid() != reference.guid)
        {
            reference.asset = LoadScriptableData(reference.guid);
            asset = ResolveScriptableDataAsset(reference.asset);
            if (asset == nullptr)
            {
                return nullptr;
            }
        }
        else if (asset->m_superseded)
        {
            // ファイルが書き直された(ReloadScriptableData)。新しい箱を引き(誰も指していなくて回収されていたら、読み直す)、
            // 読み終わってから乗り換える。読み込み中は古い中身を返すので、使う側から見て途中で消えない。
            const core::ObjectHandle newer = LoadScriptableData(reference.guid);
            const ScriptableDataAsset* newerAsset = ResolveScriptableDataAsset(newer);
            if (newerAsset != nullptr && newerAsset->GetState() != AssetState::Loading)
            {
                reference.asset = newer;
                asset = ResolveScriptableDataAsset(newer);
            }
        }

        const ScriptableData* data = GetScriptableData(*asset);
        if (data == nullptr)
        {
            return nullptr; // 読み込み中か、失敗
        }
        if (!data->GetType().IsA(expected))
        {
            if (asset->m_warnedType != &expected)
            {
                HUSKY_LOG(AssetLog::ScriptableDataWrongType, reference.guid.ToString(), data->GetType().GetName(), expected.GetName());
                asset->m_warnedType = &expected;
            }
            return nullptr;
        }
        return data;
    }

    void AssetLoader::StartScriptableDataLoad(ScriptableDataAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingScriptableDataLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読んで値の木にするだけ。中身のオブジェクト(GC 管理)は作らない。
        // ファイルを読んで JSON にする手順はプレハブと同じ(ReadJsonFile)。
        Schedule(std::move(record), [](PendingScriptableDataLoad& load) {
            load.succeeded = ReadJsonFile(load.path, load.data, load.warnings);
        });
    }

    void AssetLoader::FinishScriptableData(PendingScriptableDataLoad& load)
    {
        for (const std::string& warning : load.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "ScriptableData", load.path, warning);
        }
        ScriptableDataAsset* asset = ResolveScriptableDataAsset(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }

        // 1. 型の名前を読む
        const ArchiveValue* typeName = load.data.Find("type");
        if (typeName == nullptr || typeName->GetType() != ArchiveValue::Type::String)
        {
            HUSKY_LOG(AssetLog::ScriptableDataNoType, load.path);
            asset->m_state = AssetState::Failed;
            return;
        }

        // 2. 名前で登録簿から型を探し、ScriptableData の子かを確かめてから作る
        //    (作ってから確かめると、違う型のオブジェクトを作って捨てることになるので、先に型情報で確かめる)
        const core::TypeInfo* type = m_types.Find(typeName->AsString());
        if (type == nullptr || !type->IsA(ScriptableData::StaticType()))
        {
            if (type == nullptr)
                HUSKY_LOG(AssetLog::ScriptableDataTypeNotRegistered, typeName->AsString(), load.path);
            else
                HUSKY_LOG(AssetLog::ScriptableDataTypeNotData, typeName->AsString(), load.path);
            asset->m_state = AssetState::Failed;
            return;
        }
        core::Object* created = m_types.Create(typeName->AsString(), m_registry);
        ScriptableData* data = created != nullptr && created->IsA<ScriptableData>() ? static_cast<ScriptableData*>(created) : nullptr;
        if (data == nullptr)
        {
            HUSKY_LOG(AssetLog::ScriptableDataCreateFailed, load.path);
            if (created != nullptr)
            {
                created->Destroy();
            }
            asset->m_state = AssetState::Failed;
            return;
        }

        // 3. 値を Property 越しに入れる(知らない名前や型の違いは飛ばして警告。途中で止めない)
        const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(load.data, *data);
        for (const std::string& warning : report.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "ScriptableData", load.path, warning);
        }
        data->m_assetGuid = asset->m_guid;
        data->MarkReadOnly(); // ここから先は読むだけ(書く入口が断る。SD-2)
#if HUSKY_DEVELOPMENT
        // 生のポインタでの書き込みは印では止められないので、読み込んだときの値を控え、後で比べる(CheckScriptableDataUnchanged)。
        asset->m_snapshot = ObjectSerializer::Write(*data);
#endif
        asset->m_data = data->GetHandle(); // 箱のプロパティ(強い参照)で持つので、箱が生きている間は回収されない
        asset->m_state = AssetState::Loaded;
    }

    core::ObjectHandle AssetLoader::ReloadScriptableData(const core::Guid& guid)
    {
        // 古い箱には印を付け、キャッシュから外す(古い箱と中身は、指している AssetRef が乗り換えるまで残る)。
        const auto found = m_scriptableData.find(guid);
        if (found != m_scriptableData.end())
        {
            if (ScriptableDataAsset* old = ResolveScriptableDataAsset(found->second))
            {
                old->m_superseded = true;
            }
            m_scriptableData.erase(found);
        }
        return LoadScriptableData(guid);
    }

    std::size_t AssetLoader::ForgetScriptableData(const std::vector<const core::TypeInfo*>& types)
    {
        std::size_t count = 0;
        for (auto it = m_scriptableData.begin(); it != m_scriptableData.end();)
        {
            ScriptableDataAsset* asset = ResolveScriptableDataAsset(it->second);
            const ScriptableData* data = asset != nullptr ? GetScriptableData(*asset) : nullptr;
            bool matches = false;
            for (const core::TypeInfo* type : types)
            {
                matches = matches || (data != nullptr && type != nullptr && data->GetType().IsA(*type));
            }
            if (!matches)
            {
                ++it;
                continue;
            }
            asset->m_superseded = true;
            asset->m_data = core::ObjectHandle(); // 中身を指さなくなるので、次の GC で回収される
            it = m_scriptableData.erase(it);
            ++count;
        }
        return count;
    }

    std::size_t AssetLoader::CheckScriptableDataUnchanged()
    {
        std::size_t changedCount = 0;
#if HUSKY_DEVELOPMENT
        for (const auto& [guid, handle] : m_scriptableData)
        {
            ScriptableDataAsset* asset = ResolveScriptableDataAsset(handle);
            const ScriptableData* data = asset != nullptr ? GetScriptableData(*asset) : nullptr;
            if (data == nullptr)
            {
                continue;
            }
            // 先に、値の木を作らずに比べる(変わっていなければ確保しない。毎秒呼ばれるため)
            if (ObjectSerializer::Matches(*data, asset->m_snapshot))
            {
                continue;
            }
            const ArchiveValue current = ObjectSerializer::Write(*data);
            // どのプロパティが変わったかを、名前で出す(構造体は、中のどこが変わったかまでは見ない)。
            const ArchiveValue* now = current.Find("properties");
            const ArchiveValue* before = asset->m_snapshot.Find("properties");
            if (now != nullptr && before != nullptr)
            {
                for (std::size_t i = 0; i < now->GetMemberCount(); ++i)
                {
                    const ArchiveValue* old = before->Find(now->GetMemberName(i));
                    if (old == nullptr || !old->Equals(now->GetMemberValue(i)))
                    {
                        HUSKY_LOG(AssetLog::ReadOnlyDataModified, m_database.GetFullPath(guid), data->GetType().GetName(), guid.ToString(), now->GetMemberName(i));
                        ++changedCount;
                    }
                }
            }
            // 同じ書き換えを毎回出さないように、控えを今の値にする(次からは、新しい書き換えだけを出す)。
            asset->m_snapshot = current;
        }
#endif
        return changedCount;
    }

    void AssetLoader::OnScriptableDataDestroyed(ScriptableDataAsset& asset)
    {
        const auto found = m_scriptableData.find(asset.m_guid);
        if (found != m_scriptableData.end() && found->second == asset.GetHandle())
        {
            m_scriptableData.erase(found);
        }
    }
}
