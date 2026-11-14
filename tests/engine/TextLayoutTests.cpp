// 文字を並べる(LayoutText。UI-4)のテスト。本物のフォント(BIZ UD ゴシック、32 ピクセル)の字の幅で確かめる。GPU は使わない。
#include "engine/FontAtlas.h"
#include "engine/TextLayout.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

using husky::engine::CreateFontFace;
using husky::engine::FontAtlas;
using husky::engine::FontFace;
using husky::engine::LayoutText;
using husky::engine::TextAlign;
using husky::engine::TextLayoutOptions;
using husky::engine::TextLayoutResult;

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

    // 折り返さず、左寄せで並べたときの幅。
    float WidthOf(FontAtlas& atlas, const char* text)
    {
        TextLayoutResult result;
        LayoutText(atlas, text, TextLayoutOptions{}, result);
        return result.width;
    }

    float LineHeight(const FontAtlas& atlas)
    {
        const husky::engine::FontLineMetrics& m = atlas.GetLineMetrics();
        return m.ascent + m.descent + m.lineGap;
    }
}

TEST_CASE("TextLayout: 1行は左から並び、次の字は前の字の advance(とカーニング)だけ右。ベースラインは ascent")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    TextLayoutResult result;
    LayoutText(atlas, "AB", TextLayoutOptions{}, result);
    REQUIRE(result.glyphs.size() == 2);
    CHECK(result.lineCount == 1);
    const float advanceA = atlas.FindOrAdd(U'A')->advance;
    const float advanceB = atlas.FindOrAdd(U'B')->advance;
    const float kerning = atlas.GetKerning(U'A', U'B');
    CHECK(result.glyphs[0].penX == doctest::Approx(0.0f));
    CHECK(result.glyphs[1].penX == doctest::Approx(advanceA + kerning));
    CHECK(result.glyphs[0].baselineY == doctest::Approx(atlas.GetLineMetrics().ascent));
    CHECK(result.width == doctest::Approx(advanceA + kerning + advanceB));
    CHECK(result.height == doctest::Approx(atlas.GetLineMetrics().ascent + atlas.GetLineMetrics().descent));
}

TEST_CASE("TextLayout: 空の文字列は 0 行。空白は字として並ばず、行の終わりの空白は幅に数えない")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    TextLayoutResult result;
    LayoutText(atlas, "", TextLayoutOptions{}, result);
    CHECK(result.lineCount == 0);
    CHECK(result.glyphs.empty());
    CHECK(result.height == 0.0f);

    LayoutText(atlas, "A B ", TextLayoutOptions{}, result);
    CHECK(result.glyphs.size() == 2); // 空白は絵が無いので並ばない
    CHECK(result.width == doctest::Approx(WidthOf(atlas, "A B")));
}

TEST_CASE("TextLayout: \\n で次の行へ。次の行は左端から、ベースラインは行の高さだけ下")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    TextLayoutResult result;
    LayoutText(atlas, "AA\r\nB", TextLayoutOptions{}, result); // Windows の改行も
    REQUIRE(result.glyphs.size() == 3);
    CHECK(result.lineCount == 2);
    CHECK(result.glyphs[2].penX == doctest::Approx(0.0f));
    CHECK(result.glyphs[2].baselineY == doctest::Approx(result.glyphs[0].baselineY + LineHeight(atlas)));
    CHECK(result.width == doctest::Approx(WidthOf(atlas, "AA"))); // いちばん長い行

    // 行間を 2 倍に
    TextLayoutOptions options;
    options.lineSpacing = 2.0f;
    LayoutText(atlas, "A\nB", options, result);
    REQUIRE(result.glyphs.size() == 2);
    CHECK(result.glyphs[1].baselineY == doctest::Approx(result.glyphs[0].baselineY + LineHeight(atlas) * 2.0f));
}

TEST_CASE("TextLayout: 英語は空白の所で折り返し、単語の途中では折らない。空白は前の行の終わりで消える")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    // "AAA BBB" のうち "AAA B" までしか入らない幅 → "AAA" / "BBB"
    TextLayoutOptions options;
    options.maxWidth = WidthOf(atlas, "AAA B") + 1.0f;
    TextLayoutResult result;
    LayoutText(atlas, "AAA BBB", options, result);
    CHECK(result.lineCount == 2);
    REQUIRE(result.glyphs.size() == 6);
    CHECK(result.glyphs[3].penX == doctest::Approx(0.0f)); // 2行目の最初の B は左端
    CHECK(result.glyphs[3].baselineY > result.glyphs[2].baselineY);
    CHECK(result.lines[0].width == doctest::Approx(WidthOf(atlas, "AAA"))); // 空白は数えない
}

TEST_CASE("TextLayout: 日本語は字と字の間ならどこでも折り返す")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    // 2.5 字分の幅 → 2 字、2 字、1 字
    const float advance = atlas.FindOrAdd(U'あ')->advance;
    TextLayoutOptions options;
    options.maxWidth = advance * 2.5f;
    TextLayoutResult result;
    LayoutText(atlas, "あいうえお", options, result);
    CHECK(result.lineCount == 3);
    REQUIRE(result.glyphs.size() == 5);
    CHECK(result.glyphs[2].penX == doctest::Approx(0.0f)); // 「う」は 2 行目の左端
    CHECK(result.glyphs[4].penX == doctest::Approx(0.0f)); // 「お」は 3 行目の左端

    // 英語の後に日本語が続くときも、日本語の前で折れる
    options.maxWidth = WidthOf(atlas, "ABC") + advance * 0.5f;
    LayoutText(atlas, "ABCあい", options, result);
    CHECK(result.lineCount == 2);
}

TEST_CASE("TextLayout: 1 行より長い単語は、字の途中で折る(はみ出さない)")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);

    TextLayoutOptions options;
    options.maxWidth = WidthOf(atlas, "ABC") + 1.0f;
    TextLayoutResult result;
    LayoutText(atlas, "ABCDEFGHIJ", options, result);
    CHECK(result.lineCount >= 3);
    CHECK(result.glyphs.size() == 10); // 字は1つも失わない
    for (const TextLayoutResult::Line& line : result.lines)
    {
        CHECK(line.width <= options.maxWidth);
        CHECK(line.end > line.begin); // 空の行は作らない
    }
}

TEST_CASE("TextLayout: 中寄せ・右寄せは、箱の幅(maxWidth)から行の幅を引いた分だけずらす")
{
    const std::unique_ptr<FontFace> font = LoadTestFont();
    REQUIRE(font != nullptr);
    FontAtlas atlas(*font, 32.0f);
    const float width = WidthOf(atlas, "AB");

    TextLayoutOptions options;
    options.maxWidth = 300.0f;
    options.align = TextAlign::Center;
    TextLayoutResult result;
    LayoutText(atlas, "AB", options, result);
    REQUIRE(!result.glyphs.empty());
    CHECK(result.glyphs[0].penX == doctest::Approx((300.0f - width) * 0.5f));

    options.align = TextAlign::Right;
    LayoutText(atlas, "AB", options, result);
    CHECK(result.glyphs[0].penX == doctest::Approx(300.0f - width));

    // 折り返さないときは、いちばん長い行の幅が箱の幅
    options.maxWidth = 0.0f;
    LayoutText(atlas, "AB\nA", options, result);
    REQUIRE(result.glyphs.size() == 3);
    CHECK(result.glyphs[0].penX == doctest::Approx(0.0f));
    CHECK(result.glyphs[2].penX == doctest::Approx(width - WidthOf(atlas, "A")));
}
