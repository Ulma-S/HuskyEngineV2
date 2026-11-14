// FontFace(フォントの字を絵にする窓口。中身は stb_truetype)のテスト。UI-3a。
// exe の隣にコピーされる assets の、本物のフォント(BIZ UD ゴシック)を使う。GPU は使わない。
#include "engine/FontFace.h"

#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

using husky::engine::CreateFontFace;
using husky::engine::FontFace;
using husky::engine::FontLineMetrics;
using husky::engine::GlyphBitmap;

namespace
{
    std::unique_ptr<FontFace> LoadTestFont()
    {
        std::string data;
        if (!husky::platform::ReadTextFile(
                (husky::platform::GetExecutableDirectory() + "assets/fonts/BIZUDGothic-Regular.ttf").c_str(), data))
        {
            return nullptr;
        }
        return CreateFontFace(std::move(data));
    }

    // 絵の中に、濃さが 0 でない点(何か描かれた点)がいくつあるか。
    int CountInkedPixels(const GlyphBitmap& glyph)
    {
        int count = 0;
        for (std::uint8_t value : glyph.coverage)
        {
            if (value != 0)
            {
                ++count;
            }
        }
        return count;
    }
}

TEST_CASE("FontFace: フォントでないデータからは作れない")
{
    CHECK(CreateFontFace("") == nullptr);
    CHECK(CreateFontFace("this is not a font file") == nullptr);
}

TEST_CASE("FontFace: 英字と日本語の字を絵にできる。大きさ・置き方・濃さが入る")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    constexpr float size = 32.0f;

    GlyphBitmap glyph;
    // 'A': 絵があり、ベースラインより上に描かれる(offsetY はマイナス)。次の字までの距離は大きさより小さく 0 より大きい
    REQUIRE(font->RenderGlyph(U'A', size, glyph));
    CHECK(glyph.width > 0);
    CHECK(glyph.height > 0);
    CHECK(glyph.height <= static_cast<int>(size) + 1);
    CHECK(glyph.offsetY < 0);
    CHECK(glyph.offsetY + glyph.height <= 1); // 'A' はベースラインの下へはほとんど出ない
    CHECK(glyph.advance > 0.0f);
    CHECK(glyph.advance < size);
    CHECK(glyph.coverage.size() == static_cast<std::size_t>(glyph.width * glyph.height));
    CHECK(CountInkedPixels(glyph) > 0);

    // 'あ'(U+3042): 日本語のフォントなので字がある。全角なので、次の字までの距離は 'A' より長い
    const float advanceA = glyph.advance;
    REQUIRE(font->RenderGlyph(U'あ', size, glyph));
    CHECK(glyph.width > 0);
    CHECK(CountInkedPixels(glyph) > 0);
    CHECK(glyph.advance > advanceA);
}

TEST_CASE("FontFace: 空白は絵が無く進むだけ。フォントに無い字は false")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);

    GlyphBitmap glyph;
    REQUIRE(font->RenderGlyph(U' ', 32.0f, glyph));
    CHECK(glyph.width == 0);
    CHECK(glyph.coverage.empty());
    CHECK(glyph.advance > 0.0f);

    // U+E000 は私用領域(フォントが自由に使う番号)。このフォントには無い
    CHECK_FALSE(font->HasGlyph(U'\U0000E000'));
    CHECK_FALSE(font->RenderGlyph(U'\U0000E000', 32.0f, glyph));
    CHECK(glyph.coverage.empty());
    CHECK(font->HasGlyph(U'あ'));
}

TEST_CASE("FontFace: 大きさを 2 倍にすると、行の高さと字の幅もおよそ 2 倍。行の高さは大きさと同じ")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);

    const FontLineMetrics small = font->GetLineMetrics(16.0f);
    const FontLineMetrics large = font->GetLineMetrics(32.0f);
    CHECK(small.ascent > 0.0f);
    CHECK(small.descent > 0.0f);
    CHECK(small.ascent + small.descent == doctest::Approx(16.0f)); // pixelHeight の決まり
    CHECK(large.ascent == doctest::Approx(small.ascent * 2.0f));

    GlyphBitmap smallGlyph;
    GlyphBitmap largeGlyph;
    REQUIRE(font->RenderGlyph(U'あ', 16.0f, smallGlyph));
    REQUIRE(font->RenderGlyph(U'あ', 32.0f, largeGlyph));
    CHECK(largeGlyph.advance == doctest::Approx(smallGlyph.advance * 2.0f));
    CHECK(largeGlyph.width >= smallGlyph.width * 2 - 2); // 絵の端はピクセルに丸めるので、少しずれてよい
    CHECK(largeGlyph.width <= smallGlyph.width * 2 + 2);

    // カーニング: 無い組は 0(あるかはフォント次第なので、値の大きさだけを見る)
    CHECK(font->GetKerning(U'あ', U'い', 32.0f) == doctest::Approx(0.0f));
    CHECK(font->GetKerning(U'A', U'V', 32.0f) <= 0.0f);
}
