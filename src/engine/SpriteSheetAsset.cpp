// SpriteSheetAsset.h の実装: スプライトシートのアセットと、その読み方(SpriteSheetAssetKind)。
#include "engine/SpriteSheetAsset.h"

#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"

#include <string>
#include <utility> // std::move
#include <vector>

namespace husky::engine
{
    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // スプライトシートの読み込み中の記録。ジョブが書くのは下の結果と succeeded だけ。
        struct SpriteSheetLoad final : AssetLoad
        {
            // 1段目: .spritesheet(JSON)を読み、中の画像の GUID と、切り出しとアニメーションの一覧を取り出す。
            bool ReadSource() override
            {
                std::string text;
                ArchiveValue root;
                std::string error;
                if (!platform::ReadTextFile(path.c_str(), text))
                {
                    return false; // 理由は platform がログに出す
                }
                if (!JsonFormat::Read(text, root, &error))
                {
                    warnings.push_back("JSON として読めません: " + error);
                    return false;
                }
                const ArchiveValue* textureValue = root.Find("texture");
                if (textureValue == nullptr || textureValue->GetType() != ArchiveValue::Type::String
                    || !core::Guid::Parse(textureValue->AsString(), texture))
                {
                    warnings.push_back("texture に、画像の GUID の文字列がありません");
                    return false;
                }
                slices = ReadSpriteSlices(root, &warnings);
                animations = ReadSpriteAnimations(root, slices, &warnings);
                return true;
            }

            core::Guid texture;                      // 中で指している画像の GUID
            std::vector<SpriteSlice> slices;         // 切り出しの一覧
            std::vector<SpriteAnimation> animations; // アニメーションの一覧
            std::vector<std::string> warnings;       // 読んだときの注意(Finish がログに出す)
        };
    }

    // スプライトシートの読み方。名前のある名前空間に置くのは、SpriteSheetAsset が friend にするため。
    class SpriteSheetAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return SpriteSheetAsset::StaticType(); }
        const char* GetDisplayName() const override { return "スプライトシート"; }
        bool CanLoad(std::string_view relativePath) const override { return EndsWith(relativePath, ".spritesheet"); }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<SpriteSheetAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<SpriteSheetLoad>();
        }

        AssetState Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) override
        {
            SpriteSheetAsset& sheet = static_cast<SpriteSheetAsset&>(asset);
            SpriteSheetLoad& result = static_cast<SpriteSheetLoad&>(load);
            for (const std::string& warning : result.warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, warning);
            }
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }
            sheet.m_slices = std::move(result.slices);
            sheet.m_animations = std::move(result.animations);
            sheet.texture.guid = result.texture;
            sheet.texture.asset = {};

            // 依存の読み込み: 中で指している画像の読み込みを、すぐに始める(描くときまで待たない)。
            // Resolve は AssetRef に、読み始めたテクスチャのハンドルを書く。AssetRef はプロパティなので、
            // このスプライトシートが生きている間、テクスチャも GC に回収されない。
            loader.Resolve(sheet.texture);
            return AssetState::Loaded;
        }
    };

    SpriteSheetAsset::~SpriteSheetAsset()
    {
        ReleaseLoadedData(); // 片付けるものは無いが、決まりとして呼ぶ(後で Release を足しても漏れないように)
    }

    std::unique_ptr<AssetKind> CreateSpriteSheetAssetKind()
    {
        return std::make_unique<SpriteSheetAssetKind>();
    }
}
