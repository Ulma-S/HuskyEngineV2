// アセットの種類を、ローダを直さずに足せることのテスト(AL-4)。
// テストの中だけで、小さな種類「テキストのメモ」(.note。中身はただの文字)を作り、ローダに登録して使う。
// 新しい種類を足すときに書くのは、ここにある 3 つだけ: 箱(Asset の子)、読み込み中の記録(AssetLoad の子)、読み方(AssetKind の子)。
#include "engine/Asset.h"

#include "TestFolder.h"
#include "core/AssetRef.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

using husky::core::ObjectHandle;
using husky::engine::Application;
using husky::engine::Asset;
using husky::engine::AssetKind;
using husky::engine::AssetLoad;
using husky::engine::AssetLoader;
using husky::engine::AssetState;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;

namespace
{
    // 1. 箱: 読んだ文字を持つ。テストの exe の中だけで使う型なので HUSKY_OBJECT_INLINE でよい。
    class NoteAsset : public Asset
    {
        HUSKY_OBJECT_INLINE(NoteAsset, Asset)
    public:
        // 子のメンバーの片付け(読み方の Release)は、子のデストラクタの最初で呼ぶ決まり。
        ~NoteAsset() override { ReleaseLoadedData(); }

        const std::string& GetText() const { return m_text; }

    private:
        friend class NoteKind;
        std::string m_text;
    };

    // 2. 読み込み中の記録: ジョブ(ワーカースレッド)がファイルを読み、text に書く。
    struct NoteLoad final : AssetLoad
    {
        bool ReadSource() override { return husky::platform::ReadTextFile(path.c_str(), text); }
        // 2段目: 行の終わりの改行を落とす(「実行用のデータにする」の例)。
        bool Cook() override
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            {
                text.pop_back();
            }
            return true;
        }

        std::string text;
    };

    // 3. 読み方。回収されたときの片付けの回数を数える(Release が呼ばれることを確かめるため)。
    class NoteKind final : public AssetKind
    {
    public:
        explicit NoteKind(int& releaseCount) : m_releaseCount(releaseCount) {}

        const husky::core::TypeInfo& GetAssetType() const override { return NoteAsset::StaticType(); }
        const char* GetDisplayName() const override { return "メモ"; }
        bool CanLoad(std::string_view relativePath) const override
        {
            return relativePath.size() >= 5 && relativePath.substr(relativePath.size() - 5) == ".note";
        }
        Asset* CreateAsset(husky::core::ObjectRegistry& registry) const override { return registry.Create<NoteAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset&) const override { return std::make_unique<NoteLoad>(); }
        AssetState Finish(AssetLoader&, Asset& asset, AssetLoad& load) override
        {
            NoteLoad& result = static_cast<NoteLoad&>(load);
            if (!result.succeeded)
            {
                return AssetState::Failed;
            }
            static_cast<NoteAsset&>(asset).m_text = std::move(result.text);
            return AssetState::Loaded;
        }
        void Release(AssetLoader&, Asset&) const override { ++m_releaseCount; }

    private:
        int& m_releaseCount;
    };
}

TEST_CASE("AssetKind: テストの中で作った種類を登録すれば、ローダを直さずに読める(AL-4)")
{
    TestFolder folder("husky_test_asset_kind_note");
    folder.Write("notes/hello.note", "こんにちは\n");
    folder.Write("notes/hello.note.meta", "{\"guid\": \"" + MakeGuid(9, 1).ToString() + "\"}");

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();

    // 登録する前は、読み方が無いので読めない(何も指さないハンドル)。
    CHECK(loader.FindKind(NoteAsset::StaticType()) == nullptr);
    CHECK(loader.Load<NoteAsset>(MakeGuid(9, 1)).IsNull());

    int releaseCount = 0;
    loader.RegisterKind(std::make_unique<NoteKind>(releaseCount));
    CHECK(loader.CanLoadAs(NoteAsset::StaticType(), "notes/hello.note"));
    CHECK_FALSE(loader.CanLoadAs(NoteAsset::StaticType(), "notes/hello.txt"));

    // ハンドルで読む: すぐに戻り「読み込み中」、待つと「読めた」。同じ GUID は同じ箱。
    const ObjectHandle handle = loader.Load<NoteAsset>(MakeGuid(9, 1));
    const NoteAsset* note = loader.Resolve<NoteAsset>(handle);
    REQUIRE(note != nullptr);
    CHECK(note->GetState() == AssetState::Loading);
    loader.WaitForAllLoads();
    REQUIRE(note->IsLoaded());
    CHECK(note->GetText() == "こんにちは"); // 2段目で改行を落とした
    CHECK(loader.Load<NoteAsset>(MakeGuid(9, 1)) == handle);
    CHECK(loader.GetLoadedCount<NoteAsset>() == 1);

    // AssetRef<NoteAsset> も、ほかの種類と同じ Resolve で引ける。
    husky::core::AssetRef<NoteAsset> reference;
    reference.guid = MakeGuid(9, 1);
    CHECK(loader.Resolve(reference) == note);
    CHECK(reference.asset == handle);

    // どこからも強く指されていなければ回収され、読み方の片付け(Release)が呼ばれ、キャッシュからも外れる。
    reference = {};
    app.GetRegistry().CollectGarbage();
    CHECK(loader.Resolve<NoteAsset>(handle) == nullptr);
    CHECK(releaseCount == 1);
    CHECK(loader.GetLoadedCount<NoteAsset>() == 0);
}

TEST_CASE("AssetKind: 読み方が失敗を返すと「失敗」、ファイルが表に無ければその場で「失敗」(AL-4)")
{
    TestFolder folder("husky_test_asset_kind_note_missing");
    folder.Write("notes/gone.note.meta", "{\"guid\": \"" + MakeGuid(9, 2).ToString() + "\"}");
    folder.Write("notes/gone.note", "x");

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();
    int releaseCount = 0;
    loader.RegisterKind(std::make_unique<NoteKind>(releaseCount));

    // 表に無い GUID: パスが引けないので、ジョブを積まずに「失敗」
    const NoteAsset* unknown = loader.Resolve<NoteAsset>(loader.Load<NoteAsset>(MakeGuid(9, 99)));
    REQUIRE(unknown != nullptr);
    CHECK(unknown->GetState() == AssetState::Failed);

    // 表にはあるがファイルが消えた: 1段目が失敗し、Finish が「失敗」を返す
    folder.Remove("notes/gone.note");
    const NoteAsset* gone = loader.Resolve<NoteAsset>(loader.Load<NoteAsset>(MakeGuid(9, 2)));
    REQUIRE(gone != nullptr);
    loader.WaitForAllLoads();
    CHECK(gone->GetState() == AssetState::Failed);
}
