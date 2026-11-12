// AssetLoader.h と TextureAsset.h の実装。
#include "engine/AssetLoader.h"

#include "platform/Image.h"

#include <cstdio>  // std::printf
#include <utility> // std::move

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

    AssetLoader::AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry)
        : m_database(database)
        , m_registry(registry)
    {
        // registry は、ここではまだ使わない(Application では、このローダより後に作られるため。
        // 参照を覚えておくだけなら、相手が作られる前でもよい)。
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

        // 新しい GPU に、読めているアセットを送り直す。
        // ReadTexture はキャッシュ(m_textures)を書き換えないので、回しながら呼んでよい。
        if (m_device != nullptr)
        {
            for (const auto& entry : m_textures)
            {
                TextureAsset* asset = ResolveTexture(entry.second);
                if (asset != nullptr && asset->IsLoaded() && asset->m_texture.IsNull())
                {
                    ReadTexture(*asset);
                }
            }
        }
    }

    core::ObjectHandle AssetLoader::LoadTexture(const core::Guid& guid)
    {
        // 1. キャッシュ: まだ生きていれば、それを返す。破棄を予約済み(この後で消える)なら、作り直す。
        const auto found = m_textures.find(guid);
        if (found != m_textures.end())
        {
            const TextureAsset* cached = ResolveTexture(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        // 2〜5. アセットのオブジェクトを作り、読む。失敗しても「失敗」の状態で返す。
        TextureAsset* asset = m_registry.Create<TextureAsset>();
        if (asset == nullptr)
        {
            std::printf("アセット: オブジェクト用のメモリが足りず、テクスチャを作れません(%s)\n", guid.ToString().c_str());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        ReadTexture(*asset);
        m_textures[guid] = asset->GetHandle(); // 古いもの(破棄を予約済み)があれば置き換える
        return asset->GetHandle();
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

    void AssetLoader::ReadTexture(TextureAsset& asset)
    {
        asset.m_state = AssetState::Loading;

        // 2. GUID → パス
        const std::string path = m_database.GetFullPath(asset.m_guid);
        if (path.empty())
        {
            std::printf("アセット: GUID が表にありません(%s)\n", asset.m_guid.ToString().c_str());
            asset.m_state = AssetState::Failed;
            return;
        }

        // 3. 1段目: 元のファイルを読む
        platform::Image source;
        if (!ReadTextureSource(path, source))
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        // 4. 2段目: 実行用のデータにする(今は素通し)
        const platform::Image runtime = CookTexture(std::move(source));
        asset.m_width = runtime.width;
        asset.m_height = runtime.height;

        // 5. GPU に送る(描画の準備ができていれば)
        if (m_device != nullptr)
        {
            asset.m_texture = m_device->CreateTexture(runtime);
            if (asset.m_texture.IsNull())
            {
                asset.m_state = AssetState::Failed;
                return;
            }
        }
        asset.m_state = AssetState::Loaded;
    }

    void AssetLoader::OnTextureDestroyed(TextureAsset& asset)
    {
        if (m_device != nullptr && !asset.m_texture.IsNull())
        {
            m_device->ReleaseTexture(asset.m_texture); // GPU のメモリは、描画層が数フレーム遅らせて解放する
        }
        // キャッシュが、このアセットを指しているときだけ外す(同じ GUID で、もう新しいものを作っていることがある)。
        const auto found = m_textures.find(asset.m_guid);
        if (found != m_textures.end() && found->second == asset.GetHandle())
        {
            m_textures.erase(found);
        }
    }
}
