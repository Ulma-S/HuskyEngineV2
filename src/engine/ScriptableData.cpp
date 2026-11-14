// ScriptableData.h の実装: 書き出しと、.sdata の読み方(ScriptableDataAssetKind)、ScriptableData だけの操作。
#include "engine/ScriptableData.h"

#include "core/ObjectRegistry.h"
#include "core/TypeRegistry.h"
#include "engine/AssetDatabase.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/ObjectSerializer.h"

#include <utility> // std::move

namespace husky::engine
{
    ArchiveValue WriteScriptableData(const ScriptableData& data, std::vector<std::string>* outWarnings)
    {
        if (outWarnings != nullptr)
        {
            // ObjectSerializer::Write は、番号の対応(toId)を渡さなければハンドルを書かない。
            // 黙って消えると分かりにくいので、書かなかったプロパティの名前を残す(構造体の中のハンドルは今は数えない)。
            data.GetType().ForEachProperty([&](const core::Property& property) {
                if (property.IsReference())
                {
                    outWarnings->push_back(std::string("ハンドルのプロパティは書きません: ") + property.GetName());
                }
            });
        }
        return ObjectSerializer::Write(data);
    }

    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // .sdata の読み込み中の記録。ジョブはファイルを読んで値の木にするだけ。中身のオブジェクト(GC 管理)は作らない。
        struct ScriptableDataLoad final : AssetLoad
        {
            bool ReadSource() override { return ReadJsonAssetFile(path, data, warnings); }

            ArchiveValue data;                 // JSON を読んだ値の木({"type", "properties"})
            std::vector<std::string> warnings; // 読めなかった理由(Finish がログに出す)
        };
    }

    // .sdata の読み方。名前のある名前空間に置くのは、ScriptableData と ScriptableDataAsset が friend にするため。
    class ScriptableDataAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return ScriptableDataAsset::StaticType(); }
        const core::TypeInfo* GetContentType() const override { return &ScriptableData::StaticType(); }
        const char* GetDisplayName() const override { return "ScriptableData"; }
        // ファイルの中の "type" までは見ない(どの子の型かで絞るのは、要るようになったら)。
        bool CanLoad(std::string_view relativePath) const override { return EndsWith(relativePath, ".sdata"); }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<ScriptableDataAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<ScriptableDataLoad>();
        }

        AssetState Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) override
        {
            ScriptableDataAsset& box = static_cast<ScriptableDataAsset&>(asset);
            ScriptableDataLoad& result = static_cast<ScriptableDataLoad&>(load);
            for (const std::string& warning : result.warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, warning);
            }
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }

            // 1. 型の名前を読む
            const ArchiveValue* typeName = result.data.Find("type");
            if (typeName == nullptr || typeName->GetType() != ArchiveValue::Type::String)
            {
                HUSKY_LOG(AssetLog::ScriptableDataNoType, result.path);
                return AssetState::Failed;
            }

            // 2. 名前で登録簿から型を探し、ScriptableData の子かを確かめてから作る
            //    (作ってから確かめると、違う型のオブジェクトを作って捨てることになるので、先に型情報で確かめる)
            const core::TypeRegistry& types = loader.GetTypes();
            const core::TypeInfo* type = types.Find(typeName->AsString());
            if (type == nullptr || !type->IsA(ScriptableData::StaticType()))
            {
                if (type == nullptr)
                    HUSKY_LOG(AssetLog::ScriptableDataTypeNotRegistered, typeName->AsString(), result.path);
                else
                    HUSKY_LOG(AssetLog::ScriptableDataTypeNotData, typeName->AsString(), result.path);
                return AssetState::Failed;
            }
            core::Object* created = types.Create(typeName->AsString(), loader.GetRegistry());
            ScriptableData* data = created != nullptr && created->IsA<ScriptableData>() ? static_cast<ScriptableData*>(created) : nullptr;
            if (data == nullptr)
            {
                HUSKY_LOG(AssetLog::ScriptableDataCreateFailed, result.path);
                if (created != nullptr)
                {
                    created->Destroy();
                }
                return AssetState::Failed;
            }

            // 3. 値を Property 越しに入れる(知らない名前や型の違いは飛ばして警告。途中で止めない)
            const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(result.data, *data);
            for (const std::string& warning : report.warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path, warning);
            }
            data->m_assetGuid = box.GetGuid();
            data->MarkReadOnly(); // ここから先は読むだけ(書く入口が断る。SD-2)
#if HUSKY_DEVELOPMENT
            // 生のポインタでの書き込みは印では止められないので、読み込んだときの値を控え、後で比べる(CheckScriptableDataUnchanged)。
            box.m_snapshot = ObjectSerializer::Write(*data);
#endif
            box.m_data = data->GetHandle(); // 箱のプロパティ(強い参照)で持つので、箱が生きている間は回収されない
            return AssetState::Loaded;
        }

        const core::Object* ResolveContent(AssetLoader& loader, Asset& asset, core::AssetReference& reference,
                                           const core::TypeInfo& expected) override
        {
            ScriptableDataAsset* box = static_cast<ScriptableDataAsset*>(&asset);
            if (box->m_superseded)
            {
                // ファイルが書き直された(ReloadScriptableData)。新しい箱を引き(誰も指していなくて回収されていたら、読み直す)、
                // 読み終わってから乗り換える。読み込み中は古い中身を返すので、使う側から見て途中で消えない。
                const core::ObjectHandle newer = loader.Load(ScriptableDataAsset::StaticType(), reference.guid);
                ScriptableDataAsset* newerBox = loader.GetRegistry().Resolve<ScriptableDataAsset>(newer);
                if (newerBox != nullptr && newerBox->GetState() != AssetState::Loading)
                {
                    reference.asset = newer;
                    box = newerBox;
                }
            }

            const ScriptableData* data = GetScriptableData(loader, *box);
            if (data == nullptr)
            {
                return nullptr; // 読み込み中か、失敗
            }
            if (!data->GetType().IsA(expected))
            {
                if (box->m_warnedType != &expected)
                {
                    HUSKY_LOG(AssetLog::ScriptableDataWrongType, reference.guid.ToString(), data->GetType().GetName(),
                              expected.GetName());
                    box->m_warnedType = &expected;
                }
                return nullptr;
            }
            return data;
        }

        // ---- ScriptableData だけの操作(下の関数から呼ぶ。箱の private なメンバーを触るため、ここに置く) ----

        static void Supersede(ScriptableDataAsset& box) { box.m_superseded = true; }
        static void Forget(ScriptableDataAsset& box)
        {
            box.m_superseded = true;
            box.m_data = core::ObjectHandle(); // 中身を指さなくなるので、次の GC で回収される
        }
        static const ArchiveValue& GetSnapshot(const ScriptableDataAsset& box) { return box.m_snapshot; }
        static void SetSnapshot(ScriptableDataAsset& box, ArchiveValue snapshot) { box.m_snapshot = std::move(snapshot); }
    };

    ScriptableDataAsset::~ScriptableDataAsset()
    {
        ReleaseLoadedData(); // 片付けるものは無いが、決まりとして呼ぶ
    }

    std::unique_ptr<AssetKind> CreateScriptableDataAssetKind()
    {
        return std::make_unique<ScriptableDataAssetKind>();
    }

    const ScriptableData* GetScriptableData(const AssetLoader& loader, const ScriptableDataAsset& asset)
    {
        if (!asset.IsLoaded())
        {
            return nullptr;
        }
        return loader.GetRegistry().Resolve<ScriptableData>(asset.GetDataHandle());
    }

    core::ObjectHandle ReloadScriptableData(AssetLoader& loader, const core::Guid& guid)
    {
        // 古い箱には印を付け、キャッシュから外す(古い箱と中身は、指している AssetRef が乗り換えるまで残る)。
        const core::ObjectHandle old = loader.FindCached(ScriptableDataAsset::StaticType(), guid);
        if (ScriptableDataAsset* box = loader.GetRegistry().Resolve<ScriptableDataAsset>(old))
        {
            ScriptableDataAssetKind::Supersede(*box);
        }
        loader.ForgetCached(ScriptableDataAsset::StaticType(), guid);
        return loader.Load(ScriptableDataAsset::StaticType(), guid);
    }

    std::size_t ForgetScriptableData(AssetLoader& loader, const std::vector<const core::TypeInfo*>& types)
    {
        // 先に集めてから外す(回しながらキャッシュを書き換えないため。DLL を外すときだけ呼ぶので、確保してよい)
        std::vector<core::Guid> forget;
        loader.ForEachCached(ScriptableDataAsset::StaticType(), [&](const core::Guid& guid, core::ObjectHandle handle) {
            ScriptableDataAsset* box = loader.GetRegistry().Resolve<ScriptableDataAsset>(handle);
            const ScriptableData* data = box != nullptr ? GetScriptableData(loader, *box) : nullptr;
            bool matches = false;
            for (const core::TypeInfo* type : types)
            {
                matches = matches || (data != nullptr && type != nullptr && data->GetType().IsA(*type));
            }
            if (matches)
            {
                ScriptableDataAssetKind::Forget(*box);
                forget.push_back(guid);
            }
        });
        for (const core::Guid& guid : forget)
        {
            loader.ForgetCached(ScriptableDataAsset::StaticType(), guid);
        }
        return forget.size();
    }

    std::size_t CheckScriptableDataUnchanged(AssetLoader& loader)
    {
        std::size_t changedCount = 0;
#if HUSKY_DEVELOPMENT
        loader.ForEachCached(ScriptableDataAsset::StaticType(), [&](const core::Guid& guid, core::ObjectHandle handle) {
            ScriptableDataAsset* box = loader.GetRegistry().Resolve<ScriptableDataAsset>(handle);
            const ScriptableData* data = box != nullptr ? GetScriptableData(loader, *box) : nullptr;
            if (data == nullptr)
            {
                return;
            }
            const ArchiveValue& snapshot = ScriptableDataAssetKind::GetSnapshot(*box);
            // 先に、値の木を作らずに比べる(変わっていなければ確保しない。毎秒呼ばれるため)
            if (ObjectSerializer::Matches(*data, snapshot))
            {
                return;
            }
            ArchiveValue current = ObjectSerializer::Write(*data);
            // どのプロパティが変わったかを、名前で出す(構造体は、中のどこが変わったかまでは見ない)。
            const ArchiveValue* now = current.Find("properties");
            const ArchiveValue* before = snapshot.Find("properties");
            if (now != nullptr && before != nullptr)
            {
                for (std::size_t i = 0; i < now->GetMemberCount(); ++i)
                {
                    const ArchiveValue* old = before->Find(now->GetMemberName(i));
                    if (old == nullptr || !old->Equals(now->GetMemberValue(i)))
                    {
                        HUSKY_LOG(AssetLog::ReadOnlyDataModified, loader.GetDatabase().GetFullPath(guid),
                                  data->GetType().GetName(), guid.ToString(), now->GetMemberName(i));
                        ++changedCount;
                    }
                }
            }
            // 同じ書き換えを毎回出さないように、控えを今の値にする(次からは、新しい書き換えだけを出す)。
            ScriptableDataAssetKind::SetSnapshot(*box, std::move(current));
        });
#else
        (void)loader;
#endif
        return changedCount;
    }
}
