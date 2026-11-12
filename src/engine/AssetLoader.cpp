// AssetLoader.h と TextureAsset.h の実装。
#include "engine/AssetLoader.h"

#include "platform/Image.h"

#include <cstdio>  // std::printf
#include <utility> // std::move, std::swap

namespace husky::engine
{
    namespace
    {
        // 1段目「元のファイルを読む」: PNG を読み、CPU のメモリ上の画像にする。
        bool ReadTextureSource(const std::string& path, platform::Image& outImage)
        {
            return platform::LoadPng(path.c_str(), outImage); // 失敗の理由は platform がログに出す
        }

        // 2段目「実行用のデータにする」: 今は素通し(受け取った画像を、そのまま返す)。
        // std::move で受け取って返すので、画像のピクセルはコピーされない(中身の持ち主が移るだけ)。
        platform::Image CookTexture(platform::Image source)
        {
            return source;
        }
    }

    TextureAsset::~TextureAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnTextureDestroyed(*this);
        }
    }

    AssetLoader::AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry, core::JobSystem& jobs)
        : m_database(database)
        , m_registry(registry)
        , m_jobs(jobs)
    {
        // registry は、ここではまだ使わない(Application では、このローダより後に作られるため。
        // 参照を覚えておくだけなら、相手が作られる前でもよい)。
    }

    AssetLoader::~AssetLoader()
    {
        // ジョブは記録(m_pending の中身)に書き込むので、記録を壊す前に、すべて終わるのを待つ。
        for (const std::unique_ptr<PendingTextureLoad>& load : m_pending)
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

        // 前の GPU のテクスチャを、すべて手放す(GPU が外れる、または別の GPU に替わるため)。
        if (m_device != nullptr)
        {
            for (const auto& entry : m_textures) // entry.first が GUID、entry.second がハンドル
            {
                if (TextureAsset* asset = ResolveTexture(entry.second))
                {
                    m_device->ReleaseTexture(asset->m_texture);
                    asset->m_texture = {};
                }
            }
        }
        m_device = device;

        // 新しい GPU のために、読み終わっていて GPU のテクスチャを持っていないアセットを、読み直す。
        // (読み込み中のものは、終わったとき(Finish)に、その時点の GPU に送られるので、読み直さない)
        // StartLoad はキャッシュ(m_textures)を書き換えないので、回しながら呼んでよい。
        if (m_device != nullptr)
        {
            for (const auto& entry : m_textures)
            {
                TextureAsset* asset = ResolveTexture(entry.second);
                if (asset != nullptr && asset->IsLoaded() && asset->m_texture.IsNull())
                {
                    StartLoad(*asset);
                }
            }
        }
    }

    core::ObjectHandle AssetLoader::LoadTexture(const core::Guid& guid)
    {
        // 1. キャッシュ: まだ生きていれば、それを返す(読み込み中でも)。破棄を予約済み(この後で消える)なら、作り直す。
        const auto found = m_textures.find(guid);
        if (found != m_textures.end())
        {
            const TextureAsset* cached = ResolveTexture(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        // 2. アセットのオブジェクトを「読み込み中」で作り、ジョブを積む。
        TextureAsset* asset = m_registry.Create<TextureAsset>();
        if (asset == nullptr)
        {
            std::printf("アセット: オブジェクト用のメモリが足りず、テクスチャを作れません(%s)\n", guid.ToString().c_str());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartLoad(*asset);
        m_textures[guid] = asset->GetHandle(); // 古いもの(破棄を予約済み)があれば置き換える
        return asset->GetHandle();
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
        for (std::size_t i = 0; i < m_pending.size();)
        {
            if (!m_pending[i]->counter.IsDone())
            {
                ++i;
                continue;
            }
            Finish(*m_pending[i]);
            std::swap(m_pending[i], m_pending.back());
            m_pending.pop_back();
        }
    }

    void AssetLoader::WaitForAllLoads()
    {
        for (const std::unique_ptr<PendingTextureLoad>& load : m_pending)
        {
            m_jobs.Wait(load->counter); // 待っている間、このスレッドも積まれたジョブを実行する
        }
        Update();
    }

    TextureAsset* AssetLoader::ResolveTexture(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<TextureAsset>(handle);
    }

    bool AssetLoader::CanLoadAs(const core::TypeInfo& assetType, std::string_view relativePath)
    {
        if (assetType.IsA(TextureAsset::StaticType()))
        {
            // テクスチャとして読めるのは、今は PNG だけ(1段目の ReadTextureSource が PNG を読むため)。
            constexpr std::string_view Png = ".png";
            return relativePath.size() >= Png.size() && relativePath.substr(relativePath.size() - Png.size()) == Png;
        }
        return false;
    }

    TextureAsset* AssetLoader::Resolve(core::AssetRef<TextureAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        // 前に読んだアセットが、まだ生きていて、同じ GUID なら、それを使う(毎フレーム呼ばれるので、ここは速く済ませる)。
        TextureAsset* asset = ResolveTexture(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadTexture(reference.guid);
        return ResolveTexture(reference.asset);
    }

    void AssetLoader::StartLoad(TextureAsset& asset)
    {
        asset.m_state = AssetState::Loading;

        // 2. GUID → パス(表はゲームスレッドでだけ引く。ジョブには、引いた結果の文字列を渡す)
        std::string path = m_database.GetFullPath(asset.m_guid);
        if (path.empty())
        {
            std::printf("アセット: GUID が表にありません(%s)\n", asset.m_guid.ToString().c_str());
            asset.m_state = AssetState::Failed;
            return;
        }

        auto load = std::make_unique<PendingTextureLoad>();
        load->asset = asset.GetHandle();
        load->guid = asset.m_guid;
        load->path = std::move(path);
        PendingTextureLoad* record = load.get(); // ジョブには、記録の場所(ポインタ1つ)だけを渡す
        m_pending.push_back(std::move(load));

        // 3〜4. ジョブ(ワーカースレッド): 記録のパスを読み、記録の image と succeeded に書く。ほかは触らない。
        m_jobs.Schedule(
            [record] {
                record->succeeded = ReadTextureSource(record->path, record->image);
                if (record->succeeded)
                {
                    record->image = CookTexture(std::move(record->image));
                }
            },
            record->counter);
    }

    void AssetLoader::Finish(PendingTextureLoad& load)
    {
        // 書く先のアセットが、まだ生きていて、読み込み中のままか(途中で回収された、作り直された、なら捨てる)。
        TextureAsset* asset = ResolveTexture(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_width = load.image.width;
        asset->m_height = load.image.height;

        // 5. GPU に送る(描画の準備ができていれば)
        if (m_device != nullptr)
        {
            asset->m_texture = m_device->CreateTexture(load.image);
            if (asset->m_texture.IsNull())
            {
                asset->m_state = AssetState::Failed;
                return;
            }
        }
        asset->m_state = AssetState::Loaded;
    }

    void AssetLoader::OnTextureDestroyed(TextureAsset& asset)
    {
        if (m_device != nullptr && !asset.m_texture.IsNull())
        {
            m_device->ReleaseTexture(asset.m_texture); // GPU のメモリは、描画層が数フレーム遅らせて解放する
        }
        // キャッシュが、このアセットを指しているときだけ外す(同じ GUID で、もう新しいものを作っていることがある)。
        // 読み込み中の記録は、ここでは消さない(ジョブがまだ書き込んでいるかもしれない)。終わったときに Finish が捨てる。
        const auto found = m_textures.find(asset.m_guid);
        if (found != m_textures.end() && found->second == asset.GetHandle())
        {
            m_textures.erase(found);
        }
    }
}
