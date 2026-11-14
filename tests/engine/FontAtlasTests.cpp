// 字のアトラス(棚に詰める GlyphShelfPacker、FontAtlas)、UTF-8 の読み取り、フォントのアセットのテスト。UI-3b。
// GPU は使わない(アトラスの Upload は呼ばず、「送る予定」を見る)。
#include "engine/Application.h"
#include "engine/FontAsset.h"
#include "engine/FontAtlas.h"
#include "engine/Utf8.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using husky::engine::Application;
using husky::engine::AtlasGlyph;
using husky::engine::CreateFontFace;
using husky::engine::FontAsset;
using husky::engine::FontAtlas;
using husky::engine::FontFace;
using husky::engine::GlyphShelfPacker;

namespace
{
    const char* const TestFontPath = "fonts/BIZUDGothic-Regular.ttf";

    std::unique_ptr<FontFace> LoadTestFont()
    {
        std::string data;
        if (!husky::platform::ReadTextFile((husky::platform::GetExecutableDirectory() + "assets/" + TestFontPath).c_str(),
                                           data))
        {
            return nullptr;
        }
        return CreateFontFace(std::move(data));
    }

    // UTF-8 の文字列を、字の番号の並びにする。
    std::vector<char32_t> Decode(std::string_view text)
    {
        std::vector<char32_t> result;
        std::size_t index = 0;
        char32_t codepoint = 0;
        while (husky::engine::NextCodepoint(text, index, codepoint))
        {
            result.push_back(codepoint);
        }
        return result;
    }
}

// ---- 棚に詰める ----

TEST_CASE("GlyphShelfPacker: 左から右へ並べ、右端に着いたら、その棚でいちばん高い四角の下に次の棚を始める")
{
    GlyphShelfPacker packer(32, 32, 1); // 間を 1 空ける
    int x = 0;
    int y = 0;
    REQUIRE(packer.Insert(10, 5, x, y));
    CHECK(x == 1);
    CHECK(y == 1);
    REQUIRE(packer.Insert(10, 8, x, y)); // 同じ棚の右
    CHECK(x == 12);
    CHECK(y == 1);
    REQUIRE(packer.Insert(10, 3, x, y)); // 右端(32)の手前に入る: 23 + 10 + 1 = 34 > 32 なので次の棚へ
    CHECK(x == 1);
    CHECK(y == 1 + 8 + 1); // いちばん高かった 8 の下
    REQUIRE(packer.Insert(5, 5, x, y));
    CHECK(x == 12);
    CHECK(y == 10);
}

TEST_CASE("GlyphShelfPacker: 入らない四角は断り、置いた場所は変えない")
{
    GlyphShelfPacker packer(16, 16, 1);
    int x = -1;
    int y = -1;
    CHECK_FALSE(packer.Insert(20, 4, x, y)); // 幅が広すぎる
    CHECK_FALSE(packer.Insert(4, 0, x, y));  // 大きさ 0
    CHECK(x == -1);
    REQUIRE(packer.Insert(14, 14, x, y)); // ちょうど入る(1 + 14 + 1 = 16)
    CHECK_FALSE(packer.Insert(1, 1, x, y)); // もう空きが無い
}

// ---- UTF-8 ----

TEST_CASE("Utf8: 1〜4 バイトの字を読む")
{
    const std::vector<char32_t> codepoints = Decode("A\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80"); // A é あ 😀
    REQUIRE(codepoints.size() == 4);
    CHECK(codepoints[0] == U'A');
    CHECK(codepoints[1] == U'é');
    CHECK(codepoints[2] == U'あ');
    CHECK(codepoints[3] == U'\U0001F600');
    CHECK(Decode("").empty());
}

TEST_CASE("Utf8: 壊れた並びは置き換えの字にして、読み続ける")
{
    using husky::engine::ReplacementCharacter;
    // 続きのバイトから始まる / 途中で切れている(あ の 3 バイトのうち 2 バイト)/ 無駄に長い書き方の '/'
    const std::vector<char32_t> codepoints = Decode("\x80" "B" "\xE3\x81" "C" "\xC0\xAF");
    REQUIRE(codepoints.size() >= 4);
    CHECK(codepoints[0] == ReplacementCharacter);
    CHECK(codepoints[1] == U'B');
    CHECK(codepoints[2] == ReplacementCharacter);
    // 切れた並びは 1 バイトずつ進むので、残りの 0x81 も置き換えの字になり、その後に C が読める
    CHECK(codepoints[3] == ReplacementCharacter);
    CHECK(codepoints[4] == U'C');
    CHECK(codepoints.back() == ReplacementCharacter);
}

// ---- アトラス ----

TEST_CASE("FontAtlas: 初めての字は絵にして置き、送る予定に積む。2回目は同じものを返し、積まない")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f, 256);

    const AtlasGlyph* a = atlas.FindOrAdd(U'あ');
    REQUIRE(a != nullptr);
    CHECK(a->hasImage);
    CHECK(a->width > 0);
    CHECK(a->u1 > a->u0);
    CHECK(a->v1 > a->v0);
    CHECK(a->u1 * 256.0f - a->u0 * 256.0f == doctest::Approx(static_cast<float>(a->width))); // UV の幅 = 絵の幅
    CHECK(atlas.GetGlyphCount() == 1);
    CHECK(atlas.GetPendingUploadCount() == 1);

    CHECK(atlas.FindOrAdd(U'あ') == a); // 同じ場所を返す
    CHECK(atlas.GetPendingUploadCount() == 1);

    // 別の字は重ならない場所に置く(同じ棚なら右へ)
    const AtlasGlyph* i = atlas.FindOrAdd(U'い');
    REQUIRE(i != nullptr);
    CHECK((i->u0 >= a->u1 || i->v0 >= a->v1));
    CHECK(atlas.GetPendingUploadCount() == 2);
}

TEST_CASE("FontAtlas: 空白は絵が無く送らない。フォントに無い字は nullptr")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f, 256);

    const AtlasGlyph* space = atlas.FindOrAdd(U' ');
    REQUIRE(space != nullptr);
    CHECK_FALSE(space->hasImage);
    CHECK(space->advance > 0.0f);
    CHECK(atlas.GetPendingUploadCount() == 0);

    CHECK(atlas.FindOrAdd(U'\U0000E000') == nullptr);
    CHECK(atlas.FindOrAdd(U'\U0000E000') == nullptr); // 2回目も(覚えているので絵にし直さない)
    CHECK(atlas.GetGlyphCount() == 1);
}

TEST_CASE("FontAtlas: いっぱいになったら、それより後の字は nullptr。置けた字は使える")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f, 64); // 小さなアトラス(32 ピクセルの漢字が数個しか入らない)

    int placed = 0;
    int refused = 0;
    for (char32_t c = U'一'; c < U'一' + 40; ++c) // 漢字を 40 字
    {
        if (atlas.FindOrAdd(c) != nullptr)
        {
            ++placed;
        }
        else if (font->HasGlyph(c))
        {
            ++refused;
        }
    }
    CHECK(placed > 0);
    CHECK(refused > 0);
    CHECK(atlas.GetGlyphCount() == static_cast<std::size_t>(placed));
    CHECK(atlas.FindOrAdd(U'一') != nullptr); // 最初に置けた字は、いっぱいになった後も引ける
}

// ---- フォントのアセット ----

TEST_CASE("FontAsset: ローダで .ttf を読み、字のアトラスを大きさごとに作る")
{
    Application app; // ワーカー 0 本(同期実行)
    REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    const husky::core::Guid* guid = app.GetAssetDatabase().FindGuid(TestFontPath);
    REQUIRE(guid != nullptr);

    husky::engine::AssetLoader& loader = app.GetAssetLoader();
    const husky::core::ObjectHandle handle = loader.LoadFont(*guid);
    FontAsset* font = loader.ResolveFont(handle);
    REQUIRE(font != nullptr);
    CHECK(font->GetAtlas(16.0f) == nullptr); // 読み終わるまではアトラスを作れない

    loader.WaitForAllLoads();
    REQUIRE(font->IsLoaded());
    REQUIRE(font->GetFace() != nullptr);
    CHECK(loader.LoadFont(*guid) == handle); // 同じ GUID は同じアセット

    FontAtlas* small = font->GetAtlas(16.0f);
    FontAtlas* large = font->GetAtlas(32.0f);
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    CHECK(small != large);
    CHECK(font->GetAtlas(16.0f) == small); // 同じ大きさは同じアトラス
    CHECK(font->GetAtlasCount() == 2);
    CHECK(small->FindOrAdd(U'A') != nullptr);
}

TEST_CASE("FontAsset: フォントでないファイルは読めない(Failed)。.ttf でないパスは型で断る")
{
    husky::engine::Application kindsApp; // 読めるかどうかは、Application が登録した種類に聞く
    const husky::engine::AssetLoader& kinds = kindsApp.GetAssetLoader();
    CHECK(kinds.CanLoadAs(FontAsset::StaticType(), "fonts/a.ttf"));
    CHECK(kinds.CanLoadAs(FontAsset::StaticType(), "fonts/a.otf"));
    CHECK_FALSE(kinds.CanLoadAs(FontAsset::StaticType(), "fonts/OFL.txt"));

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    const husky::core::Guid* guid = app.GetAssetDatabase().FindGuid("fonts/OFL.txt"); // ライセンスの文(フォントではない)
    REQUIRE(guid != nullptr);
    husky::engine::AssetLoader& loader = app.GetAssetLoader();
    FontAsset* font = loader.ResolveFont(loader.LoadFont(*guid));
    REQUIRE(font != nullptr);
    loader.WaitForAllLoads();
    CHECK(font->GetState() == husky::engine::AssetState::Failed);
    CHECK(font->GetFace() == nullptr);
}
