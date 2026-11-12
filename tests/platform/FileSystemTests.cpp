// platform のファイルの読み書きのテスト。
// 本物のファイルを使う。テストの exe のあるフォルダ(build の中)に、テスト用の名前で作り、終わったら消す。
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <string>

namespace platform = husky::platform;

namespace
{
    // テスト用のファイルのパス(exe のフォルダ + 名前)。
    std::string TestPath(const char* name)
    {
        return platform::GetExecutableDirectory() + name;
    }
}

TEST_CASE("FileSystem: exe のフォルダが取れる(末尾に区切りが付く)")
{
    const std::string directory = platform::GetExecutableDirectory();
    REQUIRE_FALSE(directory.empty());
    const char last = directory.back();
    CHECK((last == '/' || last == '\\'));
}

TEST_CASE("FileSystem: 書いたものを、そのまま読み戻せる(日本語、改行、空のファイルも)")
{
    const std::string path = TestPath("husky_test_file_roundtrip.txt");
    const std::string text = "1 行目: こんにちは\n2 行目: {\"a\": 1}\r\n終わり";

    REQUIRE(platform::WriteTextFile(path.c_str(), text));
    CHECK(platform::FileExists(path.c_str()));
    CHECK_FALSE(platform::FileExists((path + ".tmp").c_str())); // 一時ファイルは残らない

    std::string loaded;
    REQUIRE(platform::ReadTextFile(path.c_str(), loaded));
    CHECK(loaded == text); // 改行(\r\n)も、変換されずにそのまま

    REQUIRE(platform::WriteTextFile(path.c_str(), "")); // 空にする(置き換え)
    REQUIRE(platform::ReadTextFile(path.c_str(), loaded));
    CHECK(loaded.empty());

    CHECK(platform::RemoveFile(path.c_str()));
    CHECK_FALSE(platform::FileExists(path.c_str()));
}

TEST_CASE("FileSystem: すでにあるファイルは、新しい中身に置き換わる")
{
    const std::string path = TestPath("husky_test_file_replace.txt");
    REQUIRE(platform::WriteTextFile(path.c_str(), "古い中身。とても長い文字列。とても長い文字列。"));
    REQUIRE(platform::WriteTextFile(path.c_str(), "新しい"));

    std::string loaded;
    REQUIRE(platform::ReadTextFile(path.c_str(), loaded));
    CHECK(loaded == "新しい"); // 古い中身の後ろが残ったりしない
    platform::RemoveFile(path.c_str());
}

TEST_CASE("FileSystem: 無いファイルは読めず、無いフォルダには書けない。無いファイルを消すのは成功扱い")
{
    std::string loaded = "前の値";
    CHECK_FALSE(platform::ReadTextFile(TestPath("husky_test_no_such_file.txt").c_str(), loaded));
    CHECK(loaded == "前の値"); // 失敗したら、受け取り先は書き換えない

    const std::string inMissingFolder = TestPath("husky_no_such_folder/file.txt");
    CHECK_FALSE(platform::WriteTextFile(inMissingFolder.c_str(), "x"));
    CHECK_FALSE(platform::FileExists(inMissingFolder.c_str()));
    CHECK(platform::RemoveFile(TestPath("husky_test_no_such_file.txt").c_str())); // もともと無い → 「無い状態」なので true
}
