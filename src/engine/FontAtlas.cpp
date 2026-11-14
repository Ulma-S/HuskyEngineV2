// FontAtlas.h の実装。
#include "engine/FontAtlas.h"

#include "engine/EngineLog.h"

#include <utility> // std::move

namespace husky::engine
{
    // ================= 棚に詰める =================

    GlyphShelfPacker::GlyphShelfPacker(int width, int height, int padding)
        : m_width(width)
        , m_height(height)
        , m_padding(padding)
        , m_cursorX(padding)
        , m_shelfY(padding)
        , m_shelfHeight(0)
    {
    }

    bool GlyphShelfPacker::Insert(int width, int height, int& outX, int& outY)
    {
        if (width <= 0 || height <= 0)
        {
            return false;
        }
        int x = m_cursorX;
        int y = m_shelfY;
        int shelfHeight = m_shelfHeight;
        // 今の棚の右端からはみ出すなら、次の棚へ(今の棚でいちばん高かった字の下)。
        if (x + width + m_padding > m_width)
        {
            x = m_padding;
            y = m_shelfY + m_shelfHeight + m_padding;
            shelfHeight = 0;
        }
        // 下からはみ出すなら、もう入らない(1つの棚に入らないほど幅が広いときも、ここで断られる)。
        if (x + width + m_padding > m_width || y + height + m_padding > m_height)
        {
            return false;
        }
        outX = x;
        outY = y;
        m_cursorX = x + width + m_padding;
        m_shelfY = y;
        m_shelfHeight = shelfHeight > height ? shelfHeight : height;
        return true;
    }

    // ================= アトラス =================

    FontAtlas::FontAtlas(const FontFace& face, float pixelHeight, int textureSize)
        : m_face(&face)
        , m_pixelHeight(pixelHeight)
        , m_textureSize(textureSize)
        , m_lineMetrics(face.GetLineMetrics(pixelHeight))
        , m_packer(textureSize, textureSize, 1) // 字の間に 1 ピクセル空ける
    {
    }

    const AtlasGlyph* FontAtlas::FindOrAdd(char32_t codepoint)
    {
        const auto found = m_glyphs.find(codepoint);
        if (found != m_glyphs.end())
        {
            return found->second.found ? &found->second.glyph : nullptr;
        }

        Entry& entry = m_glyphs[codepoint]; // まず「無い」として覚える(下で見つかれば found にする)
        if (!m_face->RenderGlyph(codepoint, m_pixelHeight, m_scratch))
        {
            return nullptr; // フォントに無い字
        }
        AtlasGlyph& glyph = entry.glyph;
        glyph.width = m_scratch.width;
        glyph.height = m_scratch.height;
        glyph.offsetX = m_scratch.offsetX;
        glyph.offsetY = m_scratch.offsetY;
        glyph.advance = m_scratch.advance;
        glyph.hasImage = m_scratch.width > 0 && m_scratch.height > 0;

        if (glyph.hasImage)
        {
            int x = 0;
            int y = 0;
            if (!m_packer.Insert(glyph.width, glyph.height, x, y))
            {
                if (!m_warnedFull)
                {
                    HUSKY_LOG(AssetLog::FontAtlasFull, static_cast<double>(m_pixelHeight), m_textureSize);
                    m_warnedFull = true;
                }
                return nullptr; // entry.found は false のまま(次からはすぐ nullptr)
            }
            const float size = static_cast<float>(m_textureSize);
            glyph.u0 = static_cast<float>(x) / size;
            glyph.v0 = static_cast<float>(y) / size;
            glyph.u1 = static_cast<float>(x + glyph.width) / size;
            glyph.v1 = static_cast<float>(y + glyph.height) / size;

            // 濃さを、白の不透明度にした RGBA の絵にして、送る予定に積む(色はスプライトの color を掛けて付ける)。
            PendingUpload upload;
            upload.x = x;
            upload.y = y;
            upload.image.width = glyph.width;
            upload.image.height = glyph.height;
            upload.image.pixels.resize(m_scratch.coverage.size() * 4);
            for (std::size_t i = 0; i < m_scratch.coverage.size(); ++i)
            {
                upload.image.pixels[i * 4 + 0] = 255;
                upload.image.pixels[i * 4 + 1] = 255;
                upload.image.pixels[i * 4 + 2] = 255;
                upload.image.pixels[i * 4 + 3] = m_scratch.coverage[i];
            }
            m_pendingUploads.push_back(std::move(upload));
        }
        entry.found = true;
        ++m_glyphCount;
        return &glyph;
    }

    void FontAtlas::Upload(render::RenderDevice& renderDevice)
    {
        if (m_pendingUploads.empty())
        {
            return;
        }
        if (m_texture.IsNull())
        {
            m_texture = renderDevice.CreateTexture(m_textureSize, m_textureSize); // 透明で始まる
            if (m_texture.IsNull())
            {
                return; // 作れなかった(ログは RenderDevice が出す)。送る予定は残し、次に試す
            }
        }
        for (const PendingUpload& upload : m_pendingUploads)
        {
            renderDevice.UpdateTexture(m_texture, upload.x, upload.y, upload.image);
        }
        m_pendingUploads.clear();
    }

    void FontAtlas::ReleaseTexture(render::RenderDevice& renderDevice)
    {
        if (!m_texture.IsNull())
        {
            renderDevice.ReleaseTexture(m_texture);
            m_texture = {};
        }
    }
}
