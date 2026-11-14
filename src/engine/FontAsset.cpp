// FontAsset.h の実装: フォントのアセットと、その読み方(FontAssetKind)。
#include "engine/FontAsset.h"

#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "platform/FileSystem.h"

#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // フォントの読み込み中の記録。ジョブが書くのは face と succeeded だけ。
        struct FontLoad final : AssetLoad
        {
            // 1段目: ファイルをそのままのバイトで読み、フォントとして開く(FontFace はただのデータなので、ジョブで作ってよい)。
            bool ReadSource() override
            {
                std::string bytes;
                if (!platform::ReadTextFile(path.c_str(), bytes))
                {
                    return false;
                }
                face = CreateFontFace(std::move(bytes));
                return face != nullptr;
            }

            std::unique_ptr<FontFace> face;
        };
    }

    // フォントの読み方。名前のある名前空間に置くのは、FontAsset が friend にするため。
    class FontAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return FontAsset::StaticType(); }
        const char* GetDisplayName() const override { return "フォント"; }
        bool CanLoad(std::string_view relativePath) const override
        {
            return EndsWith(relativePath, ".ttf") || EndsWith(relativePath, ".otf");
        }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<FontAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<FontLoad>();
        }

        bool Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) const override
        {
            (void)loader;
            FontAsset& font = static_cast<FontAsset&>(asset);
            FontLoad& result = static_cast<FontLoad&>(load);
            if (!result.succeeded)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, "could not read the file as a font");
                return false;
            }
            font.m_face = std::move(result.face);
            return true;
        }

        void Release(AssetLoader& loader, Asset& asset) const override
        {
            if (render::RenderDevice* device = loader.GetRenderDevice())
            {
                ReleaseAtlasTextures(static_cast<FontAsset&>(asset), *device);
            }
        }

        // 字のアトラスは捨てる(次に使うとき、新しい GPU で作り直す。中の字も絵にし直す)。
        void ReleaseRenderResources(AssetLoader& loader, Asset& asset, render::RenderDevice& device) const override
        {
            (void)loader;
            FontAsset& font = static_cast<FontAsset&>(asset);
            ReleaseAtlasTextures(font, device);
            font.m_atlases.clear();
        }

    private:
        static void ReleaseAtlasTextures(FontAsset& font, render::RenderDevice& device)
        {
            for (const std::unique_ptr<FontAtlas>& atlas : font.m_atlases)
            {
                atlas->ReleaseTexture(device);
            }
        }
    };

    FontAsset::~FontAsset()
    {
        ReleaseLoadedData();
    }

    FontAtlas* FontAsset::GetAtlas(float pixelHeight)
    {
        if (m_face == nullptr || pixelHeight <= 0.0f)
        {
            return nullptr;
        }
        for (const std::unique_ptr<FontAtlas>& atlas : m_atlases)
        {
            if (atlas->GetPixelHeight() == pixelHeight)
            {
                return atlas.get();
            }
        }
        m_atlases.push_back(std::make_unique<FontAtlas>(*m_face, pixelHeight));
        return m_atlases.back().get();
    }

    std::unique_ptr<AssetKind> CreateFontAssetKind()
    {
        return std::make_unique<FontAssetKind>();
    }
}
