// TextureAsset.h の実装: テクスチャのアセットと、その読み方(TextureAssetKind)。
#include "engine/TextureAsset.h"

#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "platform/Image.h"

#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // テクスチャの読み込み中の記録。ジョブが書くのは image と succeeded だけ。
        struct TextureLoad final : AssetLoad
        {
            // 1段目「元のファイルを読む」: PNG を読み、CPU のメモリ上の画像にする(失敗の理由は platform がログに出す)。
            bool ReadSource() override { return platform::LoadPng(path.c_str(), image); }

            // 2段目「実行用のデータにする」: 今は素通し。
            // 将来、GPU 向けの圧縮やミップマップ(縮小版)作りをここに入れ、結果をファイルに残しておけば、
            // 製品では1段目と2段目を飛ばして、出来上がったデータを読むだけにできる。
            bool Cook() override { return true; }

            platform::Image image;
        };
    }

    // テクスチャの読み方。名前のある名前空間に置くのは、TextureAsset が friend にするため。
    class TextureAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return TextureAsset::StaticType(); }
        const char* GetDisplayName() const override { return "テクスチャ"; }
        bool CanLoad(std::string_view relativePath) const override
        {
            return EndsWith(relativePath, ".png"); // 1段目が PNG を読むため
        }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<TextureAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<TextureLoad>();
        }

        AssetState Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) override
        {
            TextureAsset& texture = static_cast<TextureAsset&>(asset);
            TextureLoad& result = static_cast<TextureLoad&>(load);
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }
            texture.m_width = result.image.width;
            texture.m_height = result.image.height;

            // GPU に送る(描画の準備ができていれば)。GPU が無いときは、ファイルを読めたので「読めた」にする。
            if (render::RenderDevice* device = loader.GetRenderDevice())
            {
                texture.m_texture = device->CreateTexture(result.image);
                if (texture.m_texture.IsNull())
                {
                    return AssetState::Failed;
                }
            }
            return AssetState::Loaded;
        }

        void Release(AssetLoader& loader, Asset& asset) const override
        {
            TextureAsset& texture = static_cast<TextureAsset&>(asset);
            render::RenderDevice* device = loader.GetRenderDevice();
            if (device != nullptr && !texture.m_texture.IsNull())
            {
                device->ReleaseTexture(texture.m_texture); // GPU のメモリは、描画層が数フレーム遅らせて解放する
            }
        }

        void ReleaseRenderResources(AssetLoader& loader, Asset& asset, render::RenderDevice& device) const override
        {
            (void)loader;
            TextureAsset& texture = static_cast<TextureAsset&>(asset);
            device.ReleaseTexture(texture.m_texture);
            texture.m_texture = {};
        }

        // 【仮】1回目の画像を取っておけば読み直さずに済むが、メモリを使うので、今は単純に読み直す。
        bool NeedsReloadForRenderDevice(const Asset& asset) const override
        {
            return static_cast<const TextureAsset&>(asset).m_texture.IsNull();
        }
    };

    TextureAsset::~TextureAsset()
    {
        ReleaseLoadedData();
    }

    std::unique_ptr<AssetKind> CreateTextureAssetKind()
    {
        return std::make_unique<TextureAssetKind>();
    }
}
