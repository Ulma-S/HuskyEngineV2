// 字のアトラス(UI-3b): 1つのフォントの1つの大きさの字を、1枚の大きなテクスチャに詰めて置く。
// 字は使うときに初めて絵にして(FontFace::RenderGlyph)、空いている所へ置き、その範囲だけを GPU に送る
// (RenderDevice::UpdateTexture)。日本語は字が数千あり、全部を前もって絵にすると大きすぎるため(ImGui も同じやり方)。
//
// 置き場所の決め方は「棚」(GlyphShelfPacker): 左から右へ並べ、右端に着いたら、その行でいちばん高かった字の下に
// 新しい行(棚)を始める。字は高さがそろっているので、すき間が少なく、計算も軽い。
//
// GPU を触るのは Upload と ReleaseTexture だけ。字を足す・引く(FindOrAdd)は GPU を使わないのでテストできる
// (足した字の絵は「送る予定」に積んでおき、描く前に Upload でまとめて送る)。
//
// 動作スレッド: ゲームスレッド
#pragma once

#include "engine/FontFace.h"
#include "platform/Image.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace husky::engine
{
    // 四角を棚に詰めて置く場所を決める(GPU を使わない)。
    class GlyphShelfPacker
    {
    public:
        // width × height の領域に置く。padding: 四角どうしの間に空けるピクセル(隣の字がにじんで写らないように)。
        GlyphShelfPacker(int width, int height, int padding);

        // 幅 width・高さ height の四角を置く場所(左上、下が +y)を決める。入らなければ false(何も変えない)。
        bool Insert(int width, int height, int& outX, int& outY);

    private:
        int m_width;
        int m_height;
        int m_padding;
        int m_cursorX;     // 今の棚で、次に置く x
        int m_shelfY;      // 今の棚の上の y
        int m_shelfHeight; // 今の棚に置いた中で、いちばん高い四角の高さ
    };

    // アトラスに置いた字1つ。
    struct AtlasGlyph
    {
        bool hasImage = false; // 絵があるか(空白は false で、進むだけ)
        // テクスチャのどこにあるか(UV。左上が (0, 0)、右下が (1, 1))
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        // 置き方(ピクセル。FontFace.h の GlyphBitmap と同じ。offsetY は下が +)
        int width = 0;
        int height = 0;
        int offsetX = 0;
        int offsetY = 0;
        float advance = 0.0f;
    };

    class FontAtlas
    {
    public:
        // 1 辺のピクセル数の既定。1024 × 1024 に 32 ピクセルの字なら、およそ 900 字が入る。
        static constexpr int DefaultTextureSize = 1024;

        // face は借りるだけ(フォントのアセットが持ち、アトラスより長く生きる)。
        FontAtlas(const FontFace& face, float pixelHeight, int textureSize = DefaultTextureSize);

        // 字を引く。まだ置いていなければ絵にして置き、送る予定に積む。
        // 戻り値: フォントに無い字、アトラスがいっぱいで置けない字は nullptr(どちらも、2回目からは絵にし直さずにすぐ返す。
        // いっぱいの警告は1回だけ)。返したポインタは、アトラスが生きている間は使える
        // (std::unordered_map は、要素が増えて中を組み直しても、要素そのものの場所は動かさない)。
        const AtlasGlyph* FindOrAdd(char32_t codepoint);

        // まだ GPU に送っていない字を送る(最初の1回はテクスチャも作る)。描く前に呼ぶ。送る予定が無ければ何もしない。
        void Upload(render::RenderDevice& renderDevice);
        // テクスチャを手放す(フォントのアセットが回収されるとき)。
        void ReleaseTexture(render::RenderDevice& renderDevice);

        render::TextureHandle GetTexture() const { return m_texture; }
        float GetPixelHeight() const { return m_pixelHeight; }
        const FontLineMetrics& GetLineMetrics() const { return m_lineMetrics; }
        // left の次に right を並べるときの詰め(FontFace::GetKerning をこのアトラスの大きさで)。
        float GetKerning(char32_t left, char32_t right) const { return m_face->GetKerning(left, right, m_pixelHeight); }
        int GetTextureSize() const { return m_textureSize; }
        // 置いた字の数(絵の無い空白も数える。フォントに無い字・置けなかった字は数えない)と、送る予定の数。テスト用。
        std::size_t GetGlyphCount() const { return m_glyphCount; }
        std::size_t GetPendingUploadCount() const { return m_pendingUploads.size(); }

    private:
        struct Entry
        {
            bool found = false; // false: フォントに無いか、置けなかった(何度も絵にし直さないために覚えておく)
            AtlasGlyph glyph;
        };
        struct PendingUpload
        {
            int x = 0;
            int y = 0;
            platform::Image image; // 白 + 濃さを不透明度にした RGBA(色はスプライトの color で付ける)
        };

        const FontFace* m_face;
        float m_pixelHeight;
        int m_textureSize;
        FontLineMetrics m_lineMetrics;
        GlyphShelfPacker m_packer;
        std::unordered_map<char32_t, Entry> m_glyphs;
        std::size_t m_glyphCount = 0;
        std::vector<PendingUpload> m_pendingUploads;
        GlyphBitmap m_scratch; // 絵にするときの入れ物(使い回す)
        render::TextureHandle m_texture;
        bool m_warnedFull = false;
    };
}
