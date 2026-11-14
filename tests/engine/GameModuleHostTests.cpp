// HR-3: ゲームのモジュールを外す前の片付け(GameModuleHost)。DLL は使わず、表をそのまま付けて確かめる。
#include "core/AssetRef.h"
#include "core/Profiler.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/GameModule.h"
#include "engine/ScriptableData.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

using husky::core::Guid;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::GameModule;
using husky::engine::GameModuleHost;
using husky::engine::ScriptableData;

namespace
{
    // 「ゲームの型」の代わり(表の registerTypes で登録する)
    class HostGameActor : public Actor
    {
        HUSKY_OBJECT_INLINE(HostGameActor, Actor)
    };

    class HostGameSettings : public ScriptableData
    {
        HUSKY_OBJECT_INLINE(HostGameSettings, ScriptableData)
    public:
        float speed = 1.0f;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(speed)
        HUSKY_REFLECTION_END()
    };

    // ゲームの外の型(エンジンの型の代わり。ゲームの ScriptableData を AssetRef で指す)
    class HostSettingsHolder : public Actor
    {
        HUSKY_OBJECT_INLINE(HostSettingsHolder, Actor)
    public:
        husky::core::AssetRef<HostGameSettings> settings;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(settings)
        HUSKY_REFLECTION_END()
    };

    void RegisterHostGameTypes(husky::core::TypeRegistry& types)
    {
        types.Register<HostGameActor>();
        types.Register<HostGameSettings>();
    }

    GameModule MakeTestModule()
    {
        GameModule module;
        module.version = husky::engine::GameModuleVersion;
        module.registerTypes = &RegisterHostGameTypes;
        return module;
    }

    const Guid SettingsGuid = husky::tests::MakeGuid(0x7a1b2c3d4e5f4061ull, 0x9b0c1d2e3f4a5b01ull);
}

TEST_CASE("HR-3: 付けるとゲームの型を登録して覚え、ゲームの型のオブジェクトが残っている間は外すのを断る")
{
    Application app;
    GameModuleHost& host = app.GetGameModule();
    host.Attach(MakeTestModule(), app.GetTypeRegistry());
    REQUIRE(host.IsAttached());
    CHECK(host.GetTypes().size() == 2); // 付ける前からある型(エンジンの型)は入らない
    CHECK(app.GetTypeRegistry().Find("HostGameActor") != nullptr);
    CHECK(host.IsGameType(HostGameActor::StaticType()));
    CHECK_FALSE(host.IsGameType(Actor::StaticType()));

    HostGameActor* actor = app.GetWorld().Spawn<HostGameActor>();
    REQUIRE(actor != nullptr);
    CHECK(host.CountGameObjects(app.GetRegistry()) == 1);

    // シーンにいる(根から届く)ので、GC を回しても残る → 外さない
    CHECK_FALSE(host.Detach(app.GetTypeRegistry(), app.GetRegistry(), app.GetAssetLoader()));
    CHECK(host.IsAttached());
    CHECK(app.GetTypeRegistry().Find("HostGameActor") != nullptr);

    // 消すと外せる。型の登録も外れる
    actor->Destroy();
    app.GetWorld().RemoveDestroyedActors();
    CHECK(host.Detach(app.GetTypeRegistry(), app.GetRegistry(), app.GetAssetLoader()));
    CHECK_FALSE(host.IsAttached());
    CHECK(host.GetTypes().empty());
    CHECK(app.GetTypeRegistry().Find("HostGameActor") == nullptr);
    CHECK(app.GetTypeRegistry().Find("Actor") != nullptr); // エンジンの型はそのまま
    CHECK(host.CountGameObjects(app.GetRegistry()) == 0);

    // 付け直せる(ホットリロードで新しい DLL を付けるとき)
    host.Attach(MakeTestModule(), app.GetTypeRegistry());
    CHECK(app.GetTypeRegistry().Find("HostGameActor") != nullptr);
}

TEST_CASE("HR-3: 外すときは、ゲームの型の ScriptableData をローダから外すので、エンジンの型が指していても外せる")
{
    husky::tests::TestFolder folder("husky_test_module_sdata");
    folder.Write("data/settings.sdata", R"({ "type": "HostGameSettings", "properties": { "speed": 3.0 } })");
    folder.Write("data/settings.sdata.meta", husky::engine::AssetDatabase::MakeMetaText(SettingsGuid));

    Application app;
    app.GetTypeRegistry().Register<HostSettingsHolder>(); // ゲームの外の型(付ける前に登録)
    GameModuleHost& host = app.GetGameModule();
    host.Attach(MakeTestModule(), app.GetTypeRegistry());
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    husky::engine::AssetLoader& loader = app.GetAssetLoader();

    // シーンのアクター(ゲームの外の型)が、ゲームの型の ScriptableData を指す
    HostSettingsHolder* holder = app.GetWorld().Spawn<HostSettingsHolder>();
    REQUIRE(holder != nullptr);
    holder->settings.guid = SettingsGuid;
    loader.Resolve(holder->settings);
    loader.WaitForAllLoads();
    const HostGameSettings* settings = loader.Resolve(holder->settings);
    REQUIRE(settings != nullptr);
    CHECK(settings->speed == 3.0f);
    CHECK(host.CountGameObjects(app.GetRegistry()) == 1); // 中身はゲームの型

    // 箱(エンジンの型)はシーンから届くが、中身は手放すので回収され、外せる
    CHECK(host.Detach(app.GetTypeRegistry(), app.GetRegistry(), app.GetAssetLoader()));
    CHECK(loader.GetLoadedCount<husky::engine::ScriptableDataAsset>() == 0);

    // 型を付け直すと、指している AssetRef は GUID から読み直し、新しい型で中身が作られる
    host.Attach(MakeTestModule(), app.GetTypeRegistry());
    loader.Resolve(holder->settings);
    loader.WaitForAllLoads();
    const HostGameSettings* reloaded = loader.Resolve(holder->settings);
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->speed == 3.0f);
}

TEST_CASE("HR-3: プロファイラーの区間の名前は写して持つ(元の文字列が無くなっても読める)")
{
    char name[32] = "HR3.CopiedName";
    husky::core::Profiler& profiler = husky::core::Profiler::Get();
    const int section = profiler.RegisterSection(name);
    REQUIRE(section >= 0);
    name[0] = 'X'; // 元の文字列を書き換える(DLL を外すと元の文字列は無くなる)
    CHECK(std::string(profiler.GetStats(section).name) == "HR3.CopiedName");
    CHECK(profiler.RegisterSection("HR3.CopiedName") == section); // 同じ名前は同じ区間(DLL を読み直しても増えない)
}
