// FontFace.h の中身(stb_truetype)。stb_truetype を触るのはこのファイルだけ(CMake で PRIVATE にリンクする)。
// 別のライブラリに替えるときは、このファイルだけを書き直す。
#include "engine/FontFace.h"

#include "core/MemoryTracker.h"

#include <cstddef> // std::max_align_t
#include <utility> // std::move

// stb_truetype の中で使うメモリの確保も MemoryTracker を通す(数えられるように。yyjson や SDL と同じ考え)。
// u は stb が渡してくる「使う側の印」で、ここでは使わない。
#define STBTT_malloc(size, u) ((void)(u), husky::core::TrackedAllocate((size), alignof(std::max_align_t)))
#define STBTT_free(pointer, u) ((void)(u), husky::core::TrackedFree(pointer))
// 関数をこのファイルの中だけに閉じ込める(static にする)。開発ビルドでは ImGui も中に同じ名前の関数を持つので、
// 外から見える名前にすると、リンクのときにぶつかる。
#define STBTT_STATIC
// stb のヘッダは、この印を付けて1か所で読むと、宣言だけでなく中身(関数の本体)も入る。
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace husky::engine
{
    namespace
    {
        class StbFontFace final : public FontFace
        {
        public:
            // フォントの中身を受け取って持つ。stb は中身を写さずに指すだけなので、m_data はこのオブジェクトと同じだけ生きる
            // (Initialize の後に m_data を動かしたり書き換えたりしない)。
            explicit StbFontFace(std::string data)
                : m_data(std::move(data))
            {
            }

            // 読めるフォントなら true。
            bool Initialize()
            {
                const unsigned char* bytes = reinterpret_cast<const unsigned char*>(m_data.data());
                // 1つのファイルに複数のフォントが入っていることがある(.ttc)。最初の 1 つを使う。
                const int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
                if (m_data.empty() || offset < 0)
                {
                    return false;
                }
                return stbtt_InitFont(&m_info, bytes, offset) != 0;
            }

            bool HasGlyph(char32_t codepoint) const override
            {
                return FindGlyph(codepoint) != 0;
            }

            bool RenderGlyph(char32_t codepoint, float pixelHeight, GlyphBitmap& out) const override
            {
                out.width = 0;
                out.height = 0;
                out.offsetX = 0;
                out.offsetY = 0;
                out.advance = 0.0f;
                out.coverage.clear();

                const int glyph = FindGlyph(codepoint);
                if (glyph == 0)
                {
                    return false;
                }
                // フォントの中の値は「フォントの単位」(1 文字の枠が 1000 や 2048 など)。scale を掛けてピクセルにする。
                const float scale = stbtt_ScaleForPixelHeight(&m_info, pixelHeight);

                int advanceWidth = 0;
                int leftSideBearing = 0;
                stbtt_GetGlyphHMetrics(&m_info, glyph, &advanceWidth, &leftSideBearing);
                out.advance = static_cast<float>(advanceWidth) * scale;

                // 絵の四角(ペンの位置からのずれ。stb も下が +y)。
                int x0 = 0;
                int y0 = 0;
                int x1 = 0;
                int y1 = 0;
                stbtt_GetGlyphBitmapBox(&m_info, glyph, scale, scale, &x0, &y0, &x1, &y1);
                if (x1 <= x0 || y1 <= y0)
                {
                    return true; // 空白など、描くものが無い字
                }
                out.width = x1 - x0;
                out.height = y1 - y0;
                out.offsetX = x0;
                out.offsetY = y0;
                out.coverage.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0);
                // 1 行のバイト数(stride)は幅と同じ(行の間に隙間なし)。
                stbtt_MakeGlyphBitmap(&m_info, out.coverage.data(), out.width, out.height, out.width, scale, scale, glyph);
                return true;
            }

            FontLineMetrics GetLineMetrics(float pixelHeight) const override
            {
                int ascent = 0;
                int descent = 0;
                int lineGap = 0;
                stbtt_GetFontVMetrics(&m_info, &ascent, &descent, &lineGap);
                const float scale = stbtt_ScaleForPixelHeight(&m_info, pixelHeight);
                FontLineMetrics metrics;
                metrics.ascent = static_cast<float>(ascent) * scale;
                metrics.descent = static_cast<float>(-descent) * scale; // stb は下をマイナスで返すので、正の距離にする
                metrics.lineGap = static_cast<float>(lineGap) * scale;
                return metrics;
            }

            float GetKerning(char32_t left, char32_t right, float pixelHeight) const override
            {
                const int leftGlyph = FindGlyph(left);
                const int rightGlyph = FindGlyph(right);
                if (leftGlyph == 0 || rightGlyph == 0)
                {
                    return 0.0f;
                }
                const float scale = stbtt_ScaleForPixelHeight(&m_info, pixelHeight);
                return static_cast<float>(stbtt_GetGlyphKernAdvance(&m_info, leftGlyph, rightGlyph)) * scale;
            }

        private:
            // Unicode の番号から、フォントの中の字の番号を引く。無ければ 0。
            int FindGlyph(char32_t codepoint) const
            {
                return stbtt_FindGlyphIndex(&m_info, static_cast<int>(codepoint));
            }

            std::string m_data;      // フォントのファイルの中身(stb が指している)
            stbtt_fontinfo m_info{}; // stb がフォントを読むための目次(中身を指す場所など)
        };
    }

    std::unique_ptr<FontFace> CreateFontFace(std::string fileData)
    {
        auto face = std::make_unique<StbFontFace>(std::move(fileData));
        if (!face->Initialize())
        {
            return nullptr;
        }
        return face; // unique_ptr<StbFontFace> は unique_ptr<FontFace> に変わる(子から親へ)
    }
}
