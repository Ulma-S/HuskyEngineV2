// PrefabAsset.h の実装: プレハブのアセットと、その読み方(PrefabAssetKind)。
// 入れ子のプレハブ(HIER)の待ちは、この読み方が持つ(ローダは種類を知らずに Update / HasWaiting / FailWaiting を呼ぶだけ)。
#include "engine/PrefabAsset.h"

#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/Prefab.h"          // 入れ子のプレハブを見つける・開く(FindNestedPrefabs、ExpandNested)
#include "engine/SceneSerializer.h" // PrefabFinder

#include <algorithm> // std::find
#include <string>
#include <utility> // std::move, std::swap
#include <vector>

namespace husky::engine
{
    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // プレハブの読み込み中の記録。値の木はただのデータ(GC 管理のオブジェクトではない)なので、ジョブで作ってよい。
        struct PrefabLoad final : AssetLoad
        {
            bool ReadSource() override { return ReadJsonAssetFile(path, data, warnings); }

            ArchiveValue data;                 // JSON を読んだ値の木
            std::vector<std::string> warnings; // 読めなかった理由(Finish がログに出す)
        };
    }

    // プレハブの読み方。名前のある名前空間に置くのは、PrefabAsset が friend にするため。
    class PrefabAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return PrefabAsset::StaticType(); }
        const char* GetDisplayName() const override { return "プレハブ"; }
        bool CanLoad(std::string_view relativePath) const override { return EndsWith(relativePath, ".prefab"); }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<PrefabAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<PrefabLoad>();
        }

        AssetState Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) override
        {
            PrefabAsset& prefab = static_cast<PrefabAsset&>(asset);
            PrefabLoad& result = static_cast<PrefabLoad&>(load);
            for (const std::string& warning : result.warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, warning);
            }
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }
            prefab.m_raw = std::move(result.data); // 値の木を、コピーせずにアセットへ移す
            // 入れ子のプレハブ(別のプレハブから置いたもの)が無ければ、そのまま使える
            std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(prefab.m_raw);
            if (nested.empty())
            {
                prefab.m_data = prefab.m_raw;
                return AssetState::Loaded;
            }
            // 入れ子があれば、先にそれを読む(読み終わってから開いた形を作る。Update)。それまでは「読み込み中」のまま。
            Waiting waiting;
            waiting.asset = prefab.GetHandle();
            for (const core::Guid& guid : nested)
            {
                const core::ObjectHandle handle = loader.Load(PrefabAsset::StaticType(), guid);
                loader.GetRegistry().AddRoot(handle); // 待っている間に回収されないように
                waiting.nestedAssets.push_back(handle);
            }
            waiting.nested = std::move(nested);
            m_waiting.push_back(std::move(waiting));
            return AssetState::Loading;
        }

        bool Update(AssetLoader& loader) override
        {
            bool progressed = false;
            for (std::size_t i = 0; i < m_waiting.size();)
            {
                PrefabAsset* asset = loader.GetRegistry().Resolve<PrefabAsset>(m_waiting[i].asset);
                if (asset == nullptr || asset->GetState() != AssetState::Loading)
                {
                    DropWaiting(loader, i); // 回収された・読み直しで外れた
                    progressed = true;
                    continue;
                }
                bool ready = true;
                bool loop = false;
                for (const core::ObjectHandle nestedHandle : m_waiting[i].nestedAssets)
                {
                    const PrefabAsset* nested = loader.GetRegistry().Resolve<PrefabAsset>(nestedHandle);
                    if (nested != nullptr && nested->GetState() == AssetState::Loading)
                    {
                        ready = false;
                        loop = loop || nested->GetGuid() == asset->GetGuid()
                               || WaitsOn(loader, nested->GetGuid(), asset->GetGuid(), 0);
                    }
                }
                if (loop)
                {
                    HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), asset->GetGuid().ToString(),
                              "入れ子のプレハブが輪になっている(自分を中に含む)ので読めません");
                    SetState(*asset, AssetState::Failed);
                    DropWaiting(loader, i);
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
                const SceneSerializer::PrefabFinder finder = [&loader](const core::Guid& guid) -> const PrefabAsset* {
                    const PrefabAsset* nested =
                        loader.GetRegistry().Resolve<PrefabAsset>(loader.FindCached(PrefabAsset::StaticType(), guid));
                    return nested != nullptr && nested->IsLoaded() ? nested : nullptr;
                };
                asset->m_data = Prefab::ExpandNested(asset->m_raw, loader.GetRegistry(), loader.GetTypes(), finder, warnings);
                for (const std::string& warning : warnings)
                {
                    HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), asset->GetGuid().ToString(), warning);
                }
                SetState(*asset, AssetState::Loaded);
                DropWaiting(loader, i);
                progressed = true;
            }
            return progressed;
        }

        bool HasWaiting() const override { return !m_waiting.empty(); }

        void FailWaiting(AssetLoader& loader) override
        {
            while (!m_waiting.empty())
            {
                if (PrefabAsset* stuck = loader.GetRegistry().Resolve<PrefabAsset>(m_waiting.back().asset))
                {
                    SetState(*stuck, AssetState::Failed);
                }
                DropWaiting(loader, m_waiting.size() - 1);
            }
        }

    private:
        // 入れ子のプレハブが読み終わるのを待っているプレハブ。待っている間、入れ子のプレハブは根にして回収させない。
        struct Waiting
        {
            core::ObjectHandle asset;
            std::vector<core::Guid> nested;               // 入れ子のプレハブの GUID
            std::vector<core::ObjectHandle> nestedAssets; // その読み込み(根に入れている)
        };

        // 待ちの記録を外す(根も外す)。
        void DropWaiting(AssetLoader& loader, std::size_t index)
        {
            for (const core::ObjectHandle nested : m_waiting[index].nestedAssets)
            {
                loader.GetRegistry().RemoveRoot(nested);
            }
            std::swap(m_waiting[index], m_waiting.back());
            m_waiting.pop_back();
        }

        // from(待っているプレハブ)が、入れ子をたどって target を待っているか(輪の見つけ方)。
        bool WaitsOn(AssetLoader& loader, const core::Guid& from, const core::Guid& target, int depth) const
        {
            if (depth > 32)
            {
                return true; // 深すぎる: 輪とみなす
            }
            for (const Waiting& waiting : m_waiting)
            {
                const PrefabAsset* asset = loader.GetRegistry().Resolve<PrefabAsset>(waiting.asset);
                if (asset == nullptr || asset->GetGuid() != from)
                {
                    continue;
                }
                for (const core::Guid& nested : waiting.nested)
                {
                    if (nested == target || WaitsOn(loader, nested, target, depth + 1))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        std::vector<Waiting> m_waiting; // ゲームスレッドだけで触る
    };

    PrefabAsset::~PrefabAsset()
    {
        ReleaseLoadedData(); // 片付けるものは無いが、決まりとして呼ぶ
    }

    std::unique_ptr<AssetKind> CreatePrefabAssetKind()
    {
        return std::make_unique<PrefabAssetKind>();
    }

    core::ObjectHandle ReloadPrefab(AssetLoader& loader, const core::Guid& guid)
    {
        // キャッシュから外すだけ(古いアセットは、指している人がいる間は残る)。次の Load で新しく読む。
        loader.ForgetCached(PrefabAsset::StaticType(), guid);
        return loader.Load(PrefabAsset::StaticType(), guid);
    }

    std::size_t ReloadPrefabsNesting(AssetLoader& loader, const core::Guid& guid)
    {
        // 先に集めてから読み直す(読み直すとキャッシュが変わるため)
        std::vector<core::Guid> outers;
        loader.ForEachCached(PrefabAsset::StaticType(), [&](const core::Guid& cachedGuid, core::ObjectHandle handle) {
            const PrefabAsset* cached = loader.GetRegistry().Resolve<PrefabAsset>(handle);
            if (cached == nullptr || cachedGuid == guid)
            {
                return;
            }
            const std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(cached->GetRawData());
            if (std::find(nested.begin(), nested.end(), guid) != nested.end())
            {
                outers.push_back(cachedGuid);
            }
        });
        std::size_t count = 0;
        for (const core::Guid& outer : outers)
        {
            ReloadPrefab(loader, outer);
            count += 1 + ReloadPrefabsNesting(loader, outer); // その外側も(輪は読み込みで断るので、ここでは深さを気にしない)
        }
        return count;
    }
}
