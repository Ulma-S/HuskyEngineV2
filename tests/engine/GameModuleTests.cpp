// HR-2: ゲームの DLL(HuskyGame.dll)を読み込み、入口の関数から関数の表を受け取る。
// エンジンを DLL にした構成(HUSKY_GAME_DLL)だけ。DLL はテストの exe の隣にビルドされる。
#include "engine/Application.h"
#include "engine/GameModule.h"
#include "game/Game.h"
#include "platform/DynamicLibrary.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <string>

TEST_CASE("HR-2: 静的にリンクしたゲームからも、同じ形の表を受け取れる")
{
    husky::engine::GameModule module;
    husky::game::FillGameModule(module);
    CHECK(module.version == husky::engine::GameModuleVersion);
    CHECK(module.registerTypes != nullptr);
    CHECK(module.spawnInitialActors != nullptr);
    CHECK(module.onSceneReady != nullptr);
}

#if HUSKY_GAME_DLL
TEST_CASE("HR-2: ゲームの DLL を読み込むと、入口の関数から版の合った表を受け取れる")
{
    const std::string directory = husky::platform::GetExecutableDirectory();
    const std::string suffix = husky::platform::DynamicLibrary::GetFileSuffix();

    husky::platform::DynamicLibrary library;
    husky::engine::GameModule module;
    REQUIRE(husky::engine::LoadGameModule(library, directory + "HuskyGame" + suffix, module));
    CHECK(library.IsLoaded());
    CHECK(module.version == husky::engine::GameModuleVersion);
    CHECK(module.registerTypes != nullptr);
    // 表の関数は DLL の中を指す(テストの exe の中のゲームのコードとは別の写し)
    CHECK(reinterpret_cast<void*>(module.registerTypes) != reinterpret_cast<void*>(&husky::game::RegisterGameTypes));

    library.Unload();
    CHECK_FALSE(library.IsLoaded());
}

TEST_CASE("HR-2: 無いファイルや、入口の無い DLL は読み込めない(DLL は外したまま)")
{
    const std::string directory = husky::platform::GetExecutableDirectory();
    const std::string suffix = husky::platform::DynamicLibrary::GetFileSuffix();

    husky::platform::DynamicLibrary library;
    husky::engine::GameModule module;
    CHECK_FALSE(husky::engine::LoadGameModule(library, directory + "NoSuchGame" + suffix, module));
    CHECK_FALSE(library.IsLoaded());
    // エンジンの DLL は入口の関数を書き出していない
    CHECK_FALSE(husky::engine::LoadGameModule(library, directory + "HuskyEngine" + suffix, module));
    CHECK_FALSE(library.IsLoaded());
    CHECK(module.registerTypes == nullptr);
}
#endif

TEST_CASE("HR-4: 静的にリンクしたゲーム(表を直接付けた)は読み直せない")
{
    husky::engine::Application app;
    husky::engine::GameModule module;
    husky::game::FillGameModule(module);
    app.GetGameModule().Attach(module, app.GetTypeRegistry());
    const husky::engine::GameModuleReloadResult result = husky::engine::ReloadGameModule(app);
    CHECK_FALSE(result.succeeded);
    CHECK(app.GetGameModule().IsAttached()); // 何も外していない
}

#if HUSKY_GAME_DLL
TEST_CASE("HR-4: ゲームの DLL は写しから読み、読み直すとシーンのゲームの型のアクターが新しい DLL の型で作り直される")
{
    const std::string directory = husky::platform::GetExecutableDirectory();
    const std::string suffix = husky::platform::DynamicLibrary::GetFileSuffix();
    const std::string source = directory + "HuskyGame" + suffix;

    husky::engine::Application app;
    husky::engine::GameModuleHost& host = app.GetGameModule();
    REQUIRE(host.LoadFromLibrary(source, app.GetTypeRegistry()));
    const std::string firstCopy = host.GetLoadedPath();
    CHECK(firstCopy != source); // 元ではなく写しを読む(元はビルドで上書きできるまま)
    CHECK(husky::platform::FileExists(firstCopy.c_str()));

    // DLL の型(名前で作る)のアクターをシーンに置く
    husky::core::Object* created = app.GetTypeRegistry().Create("BouncerActor", app.GetRegistry());
    REQUIRE(created != nullptr);
    husky::engine::Actor* actor = husky::core::Cast<husky::engine::Actor>(created);
    REQUIRE(actor != nullptr);
    actor->name = "ReloadMe";
    REQUIRE(app.GetScene().AddLoadedActor(*actor));
    const husky::core::Guid guid = actor->guid;

    const husky::engine::GameModuleReloadResult result = husky::engine::ReloadGameModule(app);
    CHECK_MESSAGE(result.succeeded, result.message);
    CHECK(host.IsAttached());
    CHECK(host.GetLoadedPath() != firstCopy);                    // 新しい写し
    CHECK_FALSE(husky::platform::FileExists(firstCopy.c_str())); // 前の写しは消えた

    // 同じ guid のアクターが、登録し直した型で作り直されている
    husky::engine::Actor* restored = app.GetScene().FindActorByGuid(guid);
    REQUIRE(restored != nullptr);
    CHECK(restored->name == "ReloadMe");
    CHECK(&restored->GetType() == app.GetTypeRegistry().Find("BouncerActor"));
    CHECK(host.IsGameType(restored->GetType()));
}
#endif
