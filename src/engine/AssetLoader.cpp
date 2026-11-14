// AssetLoader.h と、Asset.h の箱の土台の実装。種類ごとの読み方は、種類ごとの .cpp(TextureAsset.cpp など)にある。
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"

#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"

#include <utility> // std::move, std::swap

namespace husky::engine
{
    bool ReadJsonAssetFile(const std::string& path, ArchiveValue& outData, std::vector<std::string>& outWarnings)
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

    // ================= 箱の土台(Asset)と、読み方の土台(AssetKind) =================

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

    void AssetKind::SetState(Asset& asset, AssetState state)
    {
        asset.m_state = state;
    }

    // ================= ローダ =================

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
        for (const std::unique_ptr<PendingLoad>& pending : m_pending)
        {
            m_jobs.Wait(pending->counter);
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
        // StartLoad はキャッシュを書き換えないので、回しながら呼んでよい。
        if (m_device != nullptr)
        {
            for (const KindEntry& entry : m_kinds)
            {
                for (const auto& cached : entry.cache)
                {
                    Asset* asset = m_registry.Resolve<Asset>(cached.second);
                    if (asset != nullptr && asset->IsLoaded() && entry.kind->NeedsReloadForRenderDevice(*asset))
                    {
                        StartLoad(*asset);
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
            FinishLoad(*done); // 外してから書く(Finish の中で m_pending が増えても、done は動かない)
        }
        UpdateKinds();
    }

    bool AssetLoader::UpdateKinds()
    {
        bool progressed = false;
        for (KindEntry& entry : m_kinds)
        {
            progressed = entry.kind->Update(*this) || progressed;
        }
        return progressed;
    }

    void AssetLoader::WaitForAllLoads()
    {
        // スプライトシートが読み終わると、中の画像の読み込みが新しく始まる。増えたものも含めて、無くなるまで繰り返す。
        // 種類がほかのアセットを待っている(入れ子のプレハブ)間も続ける。
        const auto anyWaiting = [this] {
            for (const KindEntry& entry : m_kinds)
            {
                if (entry.kind->HasWaiting())
                {
                    return true;
                }
            }
            return false;
        };
        while (!m_pending.empty() || anyWaiting())
        {
            for (const std::unique_ptr<PendingLoad>& pending : m_pending)
            {
                m_jobs.Wait(pending->counter); // 待っている間、このスレッドも積まれたジョブを実行する
            }
            const bool hadPending = !m_pending.empty();
            Update();
            // 読み込み中のものが無いのに、待っている種類が進まない: 待つ相手がもう来ない(念のため、読めなかったことにする)
            if (!hadPending && m_pending.empty() && anyWaiting() && !UpdateKinds())
            {
                for (KindEntry& entry : m_kinds)
                {
                    entry.kind->FailWaiting(*this);
                }
            }
        }
    }

    // ================= 種類の登録と、共通の入口 =================

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

    AssetLoader::KindEntry* AssetLoader::FindKindEntry(const core::TypeInfo& type)
    {
        for (KindEntry& entry : m_kinds)
        {
            const core::TypeInfo* content = entry.kind->GetContentType();
            if (type.IsA(entry.kind->GetAssetType()) || (content != nullptr && type.IsA(*content)))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const AssetLoader::KindEntry* AssetLoader::FindKindEntry(const core::TypeInfo& type) const
    {
        return const_cast<AssetLoader*>(this)->FindKindEntry(type); // 中身は同じ(書き換えない)
    }

    const AssetKind* AssetLoader::FindKind(const core::TypeInfo& type) const
    {
        const KindEntry* entry = FindKindEntry(type);
        return entry != nullptr ? entry->kind.get() : nullptr;
    }

    bool AssetLoader::CanLoadAs(const core::TypeInfo& type, std::string_view relativePath) const
    {
        const AssetKind* kind = FindKind(type);
        return kind != nullptr && kind->CanLoad(relativePath);
    }

    std::size_t AssetLoader::GetLoadedCount(const core::TypeInfo& type) const
    {
        const KindEntry* entry = FindKindEntry(type);
        return entry != nullptr ? entry->cache.size() : 0;
    }

    core::ObjectHandle AssetLoader::FindCached(const core::TypeInfo& type, const core::Guid& guid) const
    {
        const KindEntry* entry = FindKindEntry(type);
        if (entry == nullptr)
        {
            return {};
        }
        const auto found = entry->cache.find(guid);
        return found != entry->cache.end() ? found->second : core::ObjectHandle();
    }

    void AssetLoader::ForgetCached(const core::TypeInfo& type, const core::Guid& guid)
    {
        if (KindEntry* entry = FindKindEntry(type))
        {
            entry->cache.erase(guid); // 箱の後始末は、回収されるとき(OnAssetDestroyed は、キャッシュが別の箱を指していれば何もしない)
        }
    }

    core::ObjectHandle AssetLoader::Load(const core::TypeInfo& type, const core::Guid& guid)
    {
        KindEntry* entry = FindKindEntry(type);
        if (entry == nullptr)
        {
            HUSKY_LOG(AssetLog::KindNotRegistered, type.GetName(), guid.ToString());
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
        StartLoad(*asset);
        entry->cache[guid] = asset->GetHandle(); // 古いもの(破棄を予約済み)があれば置き換える
        return asset->GetHandle();
    }

    Asset* AssetLoader::ResolveReference(core::AssetReference& reference, const core::TypeInfo& type)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        // 箱の型(中身の型で聞かれたら、その種類の箱の型)で確かめる。
        const KindEntry* entry = FindKindEntry(type);
        const core::TypeInfo& boxType = (entry != nullptr && !type.IsA(entry->kind->GetAssetType()))
                                            ? entry->kind->GetAssetType()
                                            : type;
        // 前に読んだ箱が、まだ生きていて、同じ GUID で、型が合うなら、それを使う(毎フレーム呼ばれるので、ここは速く済ませる)。
        Asset* asset = m_registry.Resolve<Asset>(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid && asset->GetType().IsA(boxType))
        {
            return asset;
        }
        reference.asset = Load(type, reference.guid);
        asset = m_registry.Resolve<Asset>(reference.asset);
        return (asset != nullptr && asset->GetType().IsA(boxType)) ? asset : nullptr;
    }

    const core::Object* AssetLoader::ResolveContent(core::AssetReference& reference, const core::TypeInfo& expected)
    {
        KindEntry* entry = FindKindEntry(expected);
        if (entry == nullptr || entry->kind->GetContentType() == nullptr)
        {
            if (!reference.IsNull())
            {
                HUSKY_LOG(AssetLog::KindNotRegistered, expected.GetName(), reference.guid.ToString());
            }
            return nullptr;
        }
        Asset* asset = ResolveReference(reference, entry->kind->GetAssetType());
        if (asset == nullptr)
        {
            return nullptr;
        }
        return entry->kind->ResolveContent(*this, *asset, reference, expected);
    }

    // ================= 共通の流れ(読み始め → ジョブ → 結果を箱に書く → 回収) =================

    std::string AssetLoader::FindPathOrLog(const core::Guid& guid) const
    {
        std::string path = m_database.GetFullPath(guid);
        if (path.empty())
        {
            HUSKY_LOG(AssetLog::GuidNotInDatabase, guid.ToString());
        }
        return path;
    }

    void AssetLoader::StartLoad(Asset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid); // 表はゲームスレッドでだけ引く
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        // 種類の箱は、このローダが種類の表から作ったものなので、書き換えてよい読み方を表から引く。
        KindEntry* entry = FindKindEntry(asset.m_kind->GetAssetType());
        auto pending = std::make_unique<PendingLoad>();
        pending->asset = asset.GetHandle();
        pending->guid = asset.m_guid;
        pending->kind = entry->kind.get();
        pending->load = entry->kind->CreateLoad(asset);
        pending->load->guid = asset.m_guid;
        pending->load->path = std::move(path);

        // 記録を m_pending に入れてから、ジョブを積む(記録の場所は動かない)。ジョブには記録の場所(ポインタ1つ)だけを渡す。
        // ジョブ(ワーカースレッド): 種類の記録の 1段目と 2段目だけを動かす。記録の外は触らない。
        AssetLoad* load = pending->load.get();
        core::JobCounter& counter = pending->counter;
        m_pending.push_back(std::move(pending));
        m_jobs.Schedule([load] { load->succeeded = load->ReadSource() && load->Cook(); }, counter);
    }

    void AssetLoader::FinishLoad(PendingLoad& pending)
    {
        // 書く先の箱が、まだ生きていて、読み込み中のままか(途中で回収された、作り直された、なら捨てる)。
        Asset* asset = m_registry.Resolve<Asset>(pending.asset);
        if (asset == nullptr || asset->m_guid != pending.guid || asset->m_kind != pending.kind
            || asset->m_state != AssetState::Loading)
        {
            return;
        }
        asset->m_state = pending.kind->Finish(*this, *asset, *pending.load);
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
}
