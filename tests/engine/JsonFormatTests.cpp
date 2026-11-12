// ArchiveValue(値の木)と JsonFormat(JSON との相互変換)のテスト。
#include "core/MemoryTracker.h"
#include "engine/ArchiveValue.h"
#include "engine/JsonFormat.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>

using husky::engine::ArchiveValue;
namespace JsonFormat = husky::engine::JsonFormat;

namespace
{
    // テストでよく使う形の木を作る。
    ArchiveValue MakeSample()
    {
        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("name", ArchiveValue("プレイヤー \"1\"")); // 日本語と、エスケープが要る文字(")
        root.Set("hp", ArchiveValue(std::int64_t{-42}));
        root.Set("speed", ArchiveValue(1.5));
        root.Set("alive", ArchiveValue(true));
        root.Set("target", ArchiveValue()); // null
        ArchiveValue& items = root.Set("items", ArchiveValue::MakeArray());
        items.Append(ArchiveValue(std::int64_t{1}));
        items.Append(ArchiveValue(2.0)); // 小数(.0)
        items.Append(ArchiveValue::MakeObject()).Set("id", ArchiveValue(std::int64_t{7}));
        return root;
    }
}

TEST_CASE("ArchiveValue: 値の種類と、種類が違うときの既定値")
{
    CHECK(ArchiveValue().IsNull());
    CHECK(ArchiveValue(true).AsBool());
    CHECK(ArchiveValue(std::int64_t{5}).AsInt() == 5);
    CHECK(ArchiveValue(std::int64_t{5}).AsFloat() == doctest::Approx(5.0)); // 整数は小数としても読める
    CHECK(ArchiveValue(2.5).AsInt(-1) == -1);                                // 小数は整数としては読めない
    CHECK(ArchiveValue("abc").GetType() == ArchiveValue::Type::String);      // const char* は bool にならない
    CHECK(ArchiveValue(std::int64_t{5}).AsString().empty());
}

TEST_CASE("ArchiveValue: オブジェクトは足した順を保ち、同じ名前は置き換える")
{
    ArchiveValue object = ArchiveValue::MakeObject();
    object.Set("b", ArchiveValue(std::int64_t{1}));
    object.Set("a", ArchiveValue(std::int64_t{2}));
    object.Set("b", ArchiveValue(std::int64_t{3})); // 置き換え

    REQUIRE(object.GetMemberCount() == 2);
    CHECK(object.GetMemberName(0) == "b");
    CHECK(object.GetMemberValue(0).AsInt() == 3);
    CHECK(object.GetMemberName(1) == "a");
    CHECK(object.Find("a")->AsInt() == 2);
    CHECK(object.Find("none") == nullptr);
    CHECK(object.At(0).IsNull()); // オブジェクトを配列として読むと Null
}

TEST_CASE("ArchiveValue: Remove は名前のメンバーを取り除き、ほかの順番を保つ")
{
    ArchiveValue object = ArchiveValue::MakeObject();
    object.Set("a", ArchiveValue(std::int64_t{1}));
    object.Set("b", ArchiveValue(std::int64_t{2}));
    object.Set("c", ArchiveValue(std::int64_t{3}));

    CHECK(object.Remove("b"));
    CHECK_FALSE(object.Remove("b")); // もう無い
    REQUIRE(object.GetMemberCount() == 2);
    CHECK(object.GetMemberName(0) == "a");
    CHECK(object.GetMemberName(1) == "c");
    CHECK(object.Find("c")->AsInt() == 3);
    CHECK_FALSE(ArchiveValue(std::int64_t{5}).Remove("a")); // オブジェクトでなければ何もしない
}

TEST_CASE("JsonFormat: 書いて読み戻すと、同じ木になる")
{
    const ArchiveValue original = MakeSample();
    std::string text;
    REQUIRE(JsonFormat::Write(original, text));

    ArchiveValue loaded;
    std::string error;
    REQUIRE(JsonFormat::Read(text, loaded, &error));

    CHECK(loaded.Find("name")->AsString() == "プレイヤー \"1\"");
    CHECK(loaded.Find("hp")->AsInt() == -42);
    CHECK(loaded.Find("speed")->AsFloat() == doctest::Approx(1.5));
    CHECK(loaded.Find("alive")->AsBool());
    CHECK(loaded.Find("target")->IsNull());
    const ArchiveValue& items = *loaded.Find("items");
    REQUIRE(items.GetArraySize() == 3);
    CHECK(items.At(0).GetType() == ArchiveValue::Type::Int);
    CHECK(items.At(1).GetType() == ArchiveValue::Type::Float); // 2.0 は小数のまま戻る
    CHECK(items.At(2).Find("id")->AsInt() == 7);
    CHECK(loaded.GetMemberName(0) == "name"); // 順番も保たれる
}

TEST_CASE("JsonFormat: 詰めた形と読みやすい形で書ける")
{
    ArchiveValue root = ArchiveValue::MakeObject();
    root.Set("a", ArchiveValue(std::int64_t{1}));
    root.Set("b", ArchiveValue::MakeArray()).Append(ArchiveValue(true));

    std::string compact;
    REQUIRE(JsonFormat::Write(root, compact, false));
    CHECK(compact == R"({"a":1,"b":[true]})"); // R"( ... )" は生文字列。中の " や \ をそのまま書ける

    std::string pretty;
    REQUIRE(JsonFormat::Write(root, pretty, true));
    CHECK(pretty == "{\n  \"a\": 1,\n  \"b\": [\n    true\n  ]\n}");
}

TEST_CASE("JsonFormat: 数は、整数なら Int、小数点や指数があれば Float として読む")
{
    ArchiveValue loaded;
    REQUIRE(JsonFormat::Read(R"([0, -3, 9223372036854775807, 18446744073709551615, 1.25, 1e3])", loaded));
    CHECK(loaded.At(0).AsInt(-1) == 0);
    CHECK(loaded.At(1).AsInt() == -3);
    CHECK(loaded.At(2).AsInt() == std::numeric_limits<std::int64_t>::max());
    CHECK(loaded.At(3).GetType() == ArchiveValue::Type::Float); // int64 に収まらない数は小数になる
    CHECK(loaded.At(4).AsFloat() == doctest::Approx(1.25));
    CHECK(loaded.At(5).GetType() == ArchiveValue::Type::Float);
}

TEST_CASE("JsonFormat: 壊れた JSON は false を返し、理由と位置を教える")
{
    ArchiveValue loaded;
    std::string error;
    CHECK_FALSE(JsonFormat::Read(R"({"a": 1,})", loaded, &error)); // 最後の , が余計
    CHECK(error.find("バイト目") != std::string::npos);
    CHECK_FALSE(JsonFormat::Read("", loaded, &error)); // 空
    CHECK_FALSE(JsonFormat::Read("[1, 2", loaded));    // 閉じていない(理由は受け取らない)
}

TEST_CASE("JsonFormat: NaN や無限大は JSON で表せないので、書き出しに失敗する")
{
    ArchiveValue root = ArchiveValue::MakeArray();
    root.Append(ArchiveValue(std::numeric_limits<double>::quiet_NaN()));
    std::string text;
    CHECK_FALSE(JsonFormat::Write(root, text));
}

TEST_CASE("JsonFormat: yyjson の中の確保も、エンジンの入口で数えられ、終わったら全部返される")
{
    const ArchiveValue sample = MakeSample();
    const husky::core::MemoryStats before = husky::core::GetMemoryStats();
    {
        std::string text;
        JsonFormat::Write(sample, text);
        ArchiveValue loaded;
        JsonFormat::Read(text, loaded);
    } // text と loaded はここで消える
    const husky::core::MemoryStats after = husky::core::GetMemoryStats();

    CHECK(after.totalAllocations > before.totalAllocations); // 確保は入口を通った
    CHECK(after.currentBytes == before.currentBytes);        // 漏れていない
    CHECK(after.currentCount == before.currentCount);
}
