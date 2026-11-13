// アセットの窓(tools/AssetBrowser.h。SD-3b)のテスト。ImGui は画面なしで動かす(開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/AssetBrowser.h"

#include "core/MemoryTracker.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/JsonFormat.h"
#include "engine/ScriptableData.h"
#include "platform/FileSystem.h"
#include "tools/EditorCommands.h"
#include "HeadlessImGui.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::tools::AssetBrowser;
using husky::tools::EditorCommandRegistry;

namespace
{
    class BrowserSettings : public husky::engine::ScriptableData
    {
        HUSKY_OBJECT(BrowserSettings, husky::engine::ScriptableData)
    public:
        float speed = 100.0f;
        std::string label = "default";

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(speed)
            HUSKY_PROPERTY(label)
        HUSKY_REFLECTION_END()
    };

    const husky::core::Guid DataGuid = husky::tests::MakeGuid(0x6e1a2b3c4d5e4f60ull, 0x9a9b0c1d2e3f4a01ull);
    const husky::core::Guid TextureGuid = husky::tests::MakeGuid(0x6e1a2b3c4d5e4f60ull, 0x9a9b0c1d2e3f4a02ull);

    // テスト用の assets: データ1つと、開けない種類のファイル1つ。
    void WriteAssets(const husky::tests::TestFolder& folder)
    {
        folder.Write("data/settings.sdata", R"({ "type": "BrowserSettings", "properties": { "speed": 120, "label": "fast" } })");
        folder.Write("data/settings.sdata.meta", husky::engine::AssetDatabase::MakeMetaText(DataGuid));
        folder.Write("prefabs/a.prefab", "{}");
        folder.Write("prefabs/a.prefab.meta", husky::engine::AssetDatabase::MakeMetaText(TextureGuid));
    }

    const BrowserSettings* CopyOf(Application& app, const AssetBrowser& browser)
    {
        return app.GetRegistry().Resolve<BrowserSettings>(browser.GetWorkingCopy(DataGuid));
    }
}

TEST_CASE("SD-3b: データを開くと、書き換えられる写しを持つ窓が開く。写しを直すと data.set でファイルに書き、新しい中身から写しを作り直す")
{
    husky::tests::TestFolder folder("husky_test_asset_browser");
    WriteAssets(folder);
    Application app;
    app.GetTypeRegistry().Register<BrowserSettings>();
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetDataCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    husky::tests::HeadlessImGui imgui;
    const std::size_t rootsBefore = app.GetRegistry().GetRootCount();

    AssetBrowser browser;
    CHECK_FALSE(browser.OpenData(app, TextureGuid)); // .sdata でなければ開けない
    REQUIRE(browser.OpenData(app, DataGuid));
    CHECK(browser.OpenData(app, DataGuid)); // もう開いている(2つ目は開かない)
    CHECK(browser.GetOpenDataCount() == 1);

    imgui.Frame([&] { browser.Draw(app, &commands); }); // 読み始める
    app.GetAssetLoader().WaitForAllLoads();
    imgui.Frame([&] { browser.Draw(app, &commands); }); // 読めたので写しを作る
    const BrowserSettings* copy = CopyOf(app, browser);
    REQUIRE(copy != nullptr);
    CHECK_FALSE(copy->IsReadOnly()); // 写しは書き換えられる(ImGui はこれに書く)
    CHECK(copy->speed == 120.0f);
    CHECK(copy->label == "fast");
    CHECK(app.GetRegistry().GetRootCount() == rootsBefore + 2); // 写しと箱を根に入れている

    // GC が回っても、写しと箱は残る(根に入れているので)。
    app.GetRegistry().CollectGarbage();
    REQUIRE(CopyOf(app, browser) == copy);

    // 写しを直す(ImGui の部品が直したのと同じ)→ 次の Draw で data.set → ファイルが変わり、写しは新しい中身から作り直される。
    const_cast<BrowserSettings*>(copy)->speed = 300.0f;
    browser.MarkChanged(DataGuid);
    imgui.Frame([&] { browser.Draw(app, &commands); });
    std::string text;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("data/settings.sdata").c_str(), text));
    CHECK(text.find("300") != std::string::npos);
    imgui.Frame([&] { browser.Draw(app, &commands); });
    const BrowserSettings* refreshed = CopyOf(app, browser);
    REQUIRE(refreshed != nullptr);
    CHECK(refreshed->speed == 300.0f);
    CHECK(refreshed->label == "fast");
    CHECK(app.GetRegistry().GetRootCount() == rootsBefore + 2); // 箱を入れ替えても、根の数は同じ

    // Undo(前の値の data.set)でファイルが戻ると、写しも戻る。
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    imgui.Frame([&] { browser.Draw(app, &commands); });
    REQUIRE(CopyOf(app, browser) != nullptr);
    CHECK(CopyOf(app, browser)->speed == 120.0f);

    // 温まった後の毎フレームの描画は、メモリを確保しない。
    imgui.Frame([&] { browser.Draw(app, &commands); });
    const std::uint64_t allocationsBefore = husky::core::GetMemoryStats().totalAllocations;
    for (int i = 0; i < 10; ++i)
    {
        imgui.Frame([&] { browser.Draw(app, &commands); });
    }
    CHECK(husky::core::GetMemoryStats().totalAllocations == allocationsBefore);

    // 閉じると、写しと箱を根から外す。
    browser.CloseData(app, DataGuid);
    CHECK(browser.GetOpenDataCount() == 0);
    CHECK(app.GetRegistry().GetRootCount() == rootsBefore);
}

TEST_CASE("SD-3b: data.set が今は使えない(再生中など)ときは、写しを直してもファイルに書かない")
{
    husky::tests::TestFolder folder("husky_test_asset_browser_locked");
    WriteAssets(folder);
    Application app;
    app.GetTypeRegistry().Register<BrowserSettings>();
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    EditorCommandRegistry commands;
    husky::tools::EditorCommand setData = husky::tools::MakeSetDataCommand();
    setData.whyUnavailable = [](Application&) { return "Only while editing"; };
    REQUIRE(commands.Register(std::move(setData)));
    husky::tests::HeadlessImGui imgui;

    AssetBrowser browser;
    REQUIRE(browser.OpenData(app, DataGuid));
    imgui.Frame([&] { browser.Draw(app, &commands); });
    app.GetAssetLoader().WaitForAllLoads();
    imgui.Frame([&] { browser.Draw(app, &commands); });
    BrowserSettings* copy = const_cast<BrowserSettings*>(CopyOf(app, browser));
    REQUIRE(copy != nullptr);
    copy->speed = 999.0f;
    browser.MarkChanged(DataGuid);
    imgui.Frame([&] { browser.Draw(app, &commands); });
    std::string text;
    REQUIRE(husky::platform::ReadTextFile(folder.PathOf("data/settings.sdata").c_str(), text));
    CHECK(text.find("999") == std::string::npos);
}
#endif
