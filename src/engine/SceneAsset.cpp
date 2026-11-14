// SceneAsset.h の実装: シーンのアセットと、その読み方(SceneAssetKind)。形は PrefabAsset.cpp の入れ子の無い場合と同じ。
#include "engine/SceneAsset.h"

#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"

#include <string>
#include <string_view>
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

        // シーンの読み込み中の記録。値の木はただのデータなので、ジョブ(ワーカースレッド)で作ってよい。
        struct SceneLoad final : AssetLoad
        {
            bool ReadSource() override { return ReadJsonAssetFile(path, data, warnings); }

            ArchiveValue data;                 // JSON を読んだ値の木
            std::vector<std::string> warnings; // 読めなかった理由(Finish がログに出す)
        };
    }

    // シーンの読み方。名前のある名前空間に置くのは、SceneAsset が friend にするため。
    class SceneAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return SceneAsset::StaticType(); }
        const char* GetDisplayName() const override { return "シーン"; }
        bool CanLoad(std::string_view relativePath) const override { return EndsWith(relativePath, ".scene"); }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<SceneAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<SceneLoad>();
        }

        // ゲームスレッド: ジョブの結果を箱に移す。
        AssetState Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) override
        {
            (void)loader;
            SceneAsset& scene = static_cast<SceneAsset&>(asset);
            SceneLoad& result = static_cast<SceneLoad&>(load);
            for (const std::string& warning : result.warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, warning);
            }
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }
            scene.m_data = std::move(result.data); // コピーせずに移す
            return AssetState::Loaded;
        }
    };

    SceneAsset::~SceneAsset()
    {
        ReleaseLoadedData(); // 片付けるものは無いが、決まりとして呼ぶ
    }

    std::unique_ptr<AssetKind> CreateSceneAssetKind()
    {
        return std::make_unique<SceneAssetKind>();
    }
}
