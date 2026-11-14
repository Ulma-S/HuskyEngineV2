// 文字を並べる(UI-4。設計メモ「ゲーム用 UI」): UTF-8 の文字列を、字のアトラス(FontAtlas)の字で1行ずつ並べる。
//   - 行送り: "\n" で次の行へ。行の高さはフォントの ascent + descent + lineGap(に lineSpacing を掛けたもの)
//   - 折り返し: maxWidth を超える字の前で次の行へ。折ってよいのは「空白」(空白は前の行の終わりに消える)と、
//     日本語の字(ひらがな・カタカナ・漢字など)の前後。英単語の途中では折らない(1語が1行より長いときだけ、字の途中で折る)
//   - 寄せ: 行ごとに左・中・右
// 並べる計算は GPU を使わないのでテストできる。描くのは DrawTextLayout(画面のカメラの前提)。
//
// 座標の決まり(並べた結果): 箱の左上が原点、右が +x、下が +y(画像や文章と同じ向き)。字の置き方は FontFace.h の
// 「ペン」と同じで、字の絵の左上 = (penX + offsetX, baselineY + offsetY)。
//
// 後回し: 禁則(「、」「。」を行の先頭にしない)、フォントに無い字の代わりの字、1つの文字列の中で色や大きさを変える、
// 右から左へ書く言葉。
//
// 動作スレッド: ゲームスレッド(FontAtlas を書き換えるため)
#pragma once

#include "engine/FontAtlas.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace husky::engine
{
    enum class TextAlign
    {
        Left,
        Center,
        Right,
    };

    struct TextLayoutOptions
    {
        float maxWidth = 0.0f;    // この幅を超える前に折り返す(ピクセル)。0 以下なら折り返さない
        TextAlign align = TextAlign::Left;
        float lineSpacing = 1.0f; // 行の高さに掛ける(1.5 で 1.5 倍の行間)
    };

    // 並べた字1つ。
    struct TextLayoutGlyph
    {
        const AtlasGlyph* glyph = nullptr; // 絵のある字だけを並べる(空白やフォントに無い字は入らない)
        float penX = 0.0f;                 // 字のペンの位置(箱の左から)
        float baselineY = 0.0f;            // その行のベースライン(箱の上から、下が +)
    };

    // 並べた結果。入れ物を使い回せるように、呼ぶ側が持つ(毎フレーム同じ結果の入れ物に並べ直しても、温まった後は確保しない)。
    struct TextLayoutResult
    {
        std::vector<TextLayoutGlyph> glyphs;
        float width = 0.0f;  // いちばん長い行の幅(行の終わりの空白は数えない)
        float height = 0.0f; // 最初の行の上から最後の行の下まで
        int lineCount = 0;

        // 中で使う入れ物(使い回すため、ここに置く)
        struct Item
        {
            char32_t codepoint = 0;
            const AtlasGlyph* glyph = nullptr; // フォントに無い・アトラスに置けない字は nullptr(幅 0 として扱う)
        };
        struct Line
        {
            std::size_t begin = 0; // items の範囲 [begin, end)
            std::size_t end = 0;
            float width = 0.0f;
        };
        std::vector<Item> items;
        std::vector<Line> lines;
    };

    // text を並べて out に入れる(out の前の中身は消す)。初めての字は atlas に置かれる(描く前に atlas.Upload が要る)。
    void LayoutText(FontAtlas& atlas, std::string_view text, const TextLayoutOptions& options, TextLayoutResult& out);

    // 並べた字をスプライトとして積む。行列は画面ちょうどのカメラ(左下が原点、上が +y、1 単位 = 1 ピクセル)の前提で、
    // (left, top) が箱の左上の画面の位置。字は整数のピクセルに合わせる(点で読むサンプラーなので、にじまずに欠けないように)。
    // atlas.Upload を先に呼んでおくこと。
    void DrawTextLayout(render::RenderDevice& renderDevice, const FontAtlas& atlas, const TextLayoutResult& layout, float left,
                        float top, const render::Color& color);

    // 日本語の字など、前後で行を折ってよい字か(ひらがな・カタカナ・漢字・全角の記号など)。テストと LayoutText の中で使う。
    bool CanBreakAroundCodepoint(char32_t codepoint);
}
