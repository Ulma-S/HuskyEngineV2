// TextLayout.h の実装。
#include "engine/TextLayout.h"

#include "engine/Utf8.h"

#include <cmath> // std::floor

namespace husky::engine
{
    bool CanBreakAroundCodepoint(char32_t c)
    {
        // 日本語・中国語の文章は単語の間に空白を入れないので、字と字の間ならどこで折ってもよい(禁則は後回し)。
        return (c >= 0x2E80 && c <= 0x9FFF)    // CJK の部首、記号と句読点(、。「」)、ひらがな、カタカナ、漢字など
               || (c >= 0xF900 && c <= 0xFAFF) // 互換漢字
               || (c >= 0xFF00 && c <= 0xFFEF); // 全角の英数字・記号、半角カタカナ
    }

    void LayoutText(FontAtlas& atlas, std::string_view text, const TextLayoutOptions& options, TextLayoutResult& out)
    {
        out.glyphs.clear();
        out.items.clear();
        out.lines.clear();
        out.width = 0.0f;
        out.height = 0.0f;
        out.lineCount = 0;

        // 1. 字に分け、アトラスで引いておく(初めての字はここで絵にして置かれる)。
        std::size_t index = 0;
        char32_t codepoint = 0;
        while (NextCodepoint(text, index, codepoint))
        {
            if (codepoint == U'\r')
            {
                continue; // Windows の改行 "\r\n" の \r は飛ばす
            }
            TextLayoutResult::Item item;
            item.codepoint = codepoint;
            item.glyph = codepoint == U'\n' ? nullptr : atlas.FindOrAdd(codepoint);
            out.items.push_back(item);
        }

        if (out.items.empty())
        {
            return; // 空の文字列は 0 行
        }

        // 2. 行に分ける。1行ずつ、左から字を足していき、maxWidth を超える字が来たら、最後に覚えた「折ってよい所」で切る。
        const std::vector<TextLayoutResult::Item>& items = out.items;
        const std::size_t count = items.size();
        const bool wrap = options.maxWidth > 0.0f;
        constexpr std::size_t None = static_cast<std::size_t>(-1);
        std::size_t lineStart = 0;
        while (true)
        {
            float width = 0.0f;        // 行の頭からの幅(空白も含む)
            float visibleWidth = 0.0f; // 行の終わりの空白を数えない幅
            char32_t previous = 0;
            // 最後に覚えた「折ってよい所」: この行を breakEnd(含まない)で終え、次の行を breakNext から始める
            std::size_t breakEnd = None;
            std::size_t breakNext = None;
            float breakWidth = 0.0f;

            std::size_t end = count;  // この行の終わり(含まない)
            std::size_t next = None;  // 次の行の頭(None なら、これが最後の行)
            float lineWidth = 0.0f;
            std::size_t i = lineStart;
            for (; i < count; ++i)
            {
                const TextLayoutResult::Item& item = items[i];
                if (item.codepoint == U'\n')
                {
                    end = i;
                    next = i + 1;
                    break;
                }
                const bool isSpace = item.codepoint == U' ';
                // 日本語の字の前でも折ってよい(この字から次の行にできる)
                if (i > lineStart && !isSpace && CanBreakAroundCodepoint(item.codepoint))
                {
                    breakEnd = i;
                    breakNext = i;
                    breakWidth = visibleWidth;
                }
                const float kerning = (previous != 0 && item.glyph != nullptr) ? atlas.GetKerning(previous, item.codepoint) : 0.0f;
                const float advance = item.glyph != nullptr ? item.glyph->advance : 0.0f;
                const float newWidth = width + kerning + advance;
                // はみ出す(空白ははみ出しても行の終わりで消えるので、空白では切らない)
                if (wrap && !isSpace && i > lineStart && newWidth > options.maxWidth)
                {
                    if (breakNext != None && breakNext > lineStart)
                    {
                        end = breakEnd;
                        next = breakNext;
                        lineWidth = breakWidth;
                    }
                    else
                    {
                        // 折ってよい所が無い(長い英単語など): この字の前で切る
                        end = i;
                        next = i;
                        lineWidth = visibleWidth;
                    }
                    break;
                }
                width = newWidth;
                if (isSpace)
                {
                    // 空白の所で折ってよい: この行は空白の前で終え、次の行は空白の後から
                    breakEnd = i;
                    breakNext = i + 1;
                    breakWidth = visibleWidth;
                }
                else
                {
                    visibleWidth = width;
                    if (CanBreakAroundCodepoint(item.codepoint))
                    {
                        breakEnd = i + 1; // 日本語の字の後でも折ってよい
                        breakNext = i + 1;
                        breakWidth = visibleWidth;
                    }
                }
                if (item.glyph != nullptr)
                {
                    previous = item.codepoint;
                }
            }
            if (i >= count && next == None)
            {
                end = count;
                lineWidth = visibleWidth;
            }
            else if (next != None && end == i && items[i].codepoint == U'\n')
            {
                lineWidth = visibleWidth; // "\n" で終わった行
            }

            TextLayoutResult::Line line;
            line.begin = lineStart;
            line.end = end;
            line.width = lineWidth;
            out.lines.push_back(line);
            if (lineWidth > out.width)
            {
                out.width = lineWidth;
            }
            if (next == None)
            {
                break;
            }
            lineStart = next;
        }

        // 3. 行ごとに寄せて、絵のある字を並べる。
        const FontLineMetrics& metrics = atlas.GetLineMetrics();
        const float lineHeight = (metrics.ascent + metrics.descent + metrics.lineGap) * options.lineSpacing;
        const float boxWidth = wrap ? options.maxWidth : out.width;
        for (std::size_t lineIndex = 0; lineIndex < out.lines.size(); ++lineIndex)
        {
            const TextLayoutResult::Line& line = out.lines[lineIndex];
            float penX = 0.0f;
            if (options.align == TextAlign::Center)
            {
                penX = (boxWidth - line.width) * 0.5f;
            }
            else if (options.align == TextAlign::Right)
            {
                penX = boxWidth - line.width;
            }
            const float baselineY = metrics.ascent + lineHeight * static_cast<float>(lineIndex);
            char32_t previous = 0;
            for (std::size_t i = line.begin; i < line.end; ++i)
            {
                const TextLayoutResult::Item& item = items[i];
                if (item.glyph == nullptr)
                {
                    continue; // フォントに無い・置けなかった字は幅 0 で飛ばす
                }
                if (previous != 0)
                {
                    penX += atlas.GetKerning(previous, item.codepoint);
                }
                if (item.glyph->hasImage)
                {
                    out.glyphs.push_back(TextLayoutGlyph{item.glyph, penX, baselineY});
                }
                penX += item.glyph->advance;
                previous = item.codepoint;
            }
        }
        out.lineCount = static_cast<int>(out.lines.size());
        out.height = out.lineCount > 0
                         ? lineHeight * static_cast<float>(out.lineCount - 1) + metrics.ascent + metrics.descent
                         : 0.0f;
    }

    void DrawTextLayout(render::RenderDevice& renderDevice, const FontAtlas& atlas, const TextLayoutResult& layout, float left,
                        float top, const render::Color& color)
    {
        for (const TextLayoutGlyph& placed : layout.glyphs)
        {
            const AtlasGlyph& glyph = *placed.glyph;
            // 箱の中の位置(下が +)を、画面の位置(上が +)にする。字の絵の左上を、整数のピクセルに合わせる。
            const float x = std::floor(left + placed.penX + 0.5f) + static_cast<float>(glyph.offsetX);
            const float y = std::floor(top - placed.baselineY + 0.5f) - static_cast<float>(glyph.offsetY);
            render::Sprite sprite;
            sprite.model.m[12] = x;
            sprite.model.m[13] = y;
            sprite.width = static_cast<float>(glyph.width);
            sprite.height = static_cast<float>(glyph.height);
            sprite.pivotX = 0.0f; // 左上の角を位置に合わせる
            sprite.pivotY = 1.0f;
            sprite.color = color;
            sprite.texture = atlas.GetTexture();
            sprite.u0 = glyph.u0;
            sprite.v0 = glyph.v0;
            sprite.u1 = glyph.u1;
            sprite.v1 = glyph.v1;
            renderDevice.DrawSprite(sprite);
        }
    }
}
