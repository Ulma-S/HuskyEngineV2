// MemoryHooks(SDL のメモリの確保を MemoryTracker に通す)のテスト。
// テストの exe の main(tests/TestMain.cpp)が、最初に InstallMemoryHooks を呼んでいる。
#include "platform/MemoryHooks.h"

#include "core/MemoryTracker.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <string>

namespace platform = husky::platform;
using husky::core::GetMemoryStats;
using husky::core::MemoryStats;

TEST_CASE("MemoryHooks: テストの main で差し替え済み。2回目に呼んでも何も起きない")
{
    CHECK(platform::AreMemoryHooksInstalled());
    CHECK(platform::InstallMemoryHooks());
}

TEST_CASE("MemoryHooks: SDL の中の確保も MemoryTracker に数えられ、返すと元に戻る")
{
    const std::string path = platform::GetExecutableDirectory() + "husky_test_memory_hooks.txt";
    const std::string text(4096, 'x'); // 4 KB。SDL_LoadFile は、ファイルの大きさ以上を SDL_malloc で確保する
    REQUIRE(platform::WriteTextFile(path.c_str(), text));

    // ReadTextFile の中で、SDL_LoadFile が SDL_malloc で確保し、読み終えたら SDL_free で返す。
    // std::string loaded を先に十分な大きさにしておき、std::string 側の確保が数に混ざらないようにする。
    std::string loaded;
    loaded.reserve(8192);
    const MemoryStats before = GetMemoryStats();
    const bool read = platform::ReadTextFile(path.c_str(), loaded);
    const MemoryStats after = GetMemoryStats();
    platform::RemoveFile(path.c_str());

    REQUIRE(read);
    CHECK(loaded == text);
    // 差し替えていなければ、SDL の確保は数えられず、回数は 0 のまま。
    CHECK(after.totalAllocations > before.totalAllocations);
    // SDL が返した(SDL_free)ので、確保中の量は元に戻る(ReadTextFile の中で、SDL のメモリは残らない)。
    CHECK(after.currentBytes == before.currentBytes);
}
