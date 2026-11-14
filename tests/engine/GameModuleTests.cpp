// HR-2: ゲームの DLL(HuskyGame.dll)を読み込み、入口の関数から関数の表を受け取る。
// エンジンを DLL にした構成(HUSKY_GAME_DLL)だけ。DLL はテストの exe の隣にビルドされる。
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
