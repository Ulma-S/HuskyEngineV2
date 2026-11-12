// Guid(128 ビットの ID)のテスト。
#include "core/Guid.h"

#include <doctest/doctest.h>

#include <string>
#include <unordered_set>

using husky::core::Guid;

TEST_CASE("Guid: 既定はすべて 0 で、どこも指さない")
{
    const Guid guid;
    CHECK(guid.IsNull());
    CHECK(guid.ToString() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("Guid: 文字列にして読み戻すと、同じ GUID になる")
{
    Guid guid;
    guid.high = 0x3f2a9c1e7b404d2eull;
    guid.low = 0x9a1f0c5e8b7d6a43ull;
    CHECK(guid.ToString() == "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43");

    Guid loaded;
    REQUIRE(Guid::Parse(guid.ToString(), loaded));
    CHECK(loaded == guid);
}

TEST_CASE("Guid: 大文字も読める。形が違う文字列は読まず、受け取り先を変えない")
{
    Guid loaded;
    REQUIRE(Guid::Parse("3F2A9C1E-7B40-4D2E-9A1F-0C5E8B7D6A43", loaded));
    CHECK(loaded.ToString() == "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43");

    const Guid before = loaded;
    CHECK_FALSE(Guid::Parse("", loaded));
    CHECK_FALSE(Guid::Parse("3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a4", loaded));   // 1 文字足りない
    CHECK_FALSE(Guid::Parse("3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a430", loaded)); // 1 文字多い
    CHECK_FALSE(Guid::Parse("3f2a9c1e_7b40-4d2e-9a1f-0c5e8b7d6a43", loaded));  // ハイフンの位置に別の文字
    CHECK_FALSE(Guid::Parse("3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a4g", loaded));  // 16 進数でない文字
    CHECK(loaded == before);
}

TEST_CASE("Guid: Generate は、バージョン 4 の印を持ち、たくさん作っても重ならない")
{
    std::unordered_set<Guid> seen; // std::hash<Guid> があるので、ハッシュの集合に入れられる
    for (int i = 0; i < 10000; ++i)
    {
        const Guid guid = Guid::Generate();
        CHECK_FALSE(guid.IsNull());
        const std::string text = guid.ToString();
        CHECK(text[14] == '4'); // バージョン 4(乱数で作った UUID)
        CHECK((text[19] == '8' || text[19] == '9' || text[19] == 'a' || text[19] == 'b')); // バリアントの印
        CHECK(seen.insert(guid).second); // insert の second は「新しく入った(重なっていなかった)」なら true
    }
}

TEST_CASE("Guid: 比べたり並べたりできる")
{
    Guid a;
    a.high = 1;
    Guid b;
    b.high = 1;
    b.low = 2;
    CHECK(a != b);
    CHECK(a < b);
    CHECK_FALSE(b < a);
}
