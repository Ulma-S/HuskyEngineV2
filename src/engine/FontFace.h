// フォント(TTF)から字の絵を作る窓口(UI-3a。設計メモ「ゲーム用 UI」)。
// 中身は stb_truetype(engine/StbFontFace.cpp)。このヘッダには stb の型を出さないので、別のライブラリ(FreeType など)に
// 替えるときは StbFontFace.cpp だけを書き直せばよい(音の AudioOutput、物理の PhysicsWorld2D と同じ形)。
//
// 座標の決まり(フォントの世界の決まりに合わせた): 字はベースライン(字が乗る線)の上の「ペン」の位置から描く。
//   右が +x、下が +y(画像と同じ向き。ベースラインより上は y がマイナス)。単位はピクセル。
//
//          offsetX
//   ペン ●--→┌──────┐ ↑ offsetY(マイナス = ベースラインより上)
//            │ 字の絵 │
//   ─────────┼──────┼──── ベースライン
//            └──────┘
//   ●────────────────→● 次の字のペン(advance だけ右)
//
// 動作スレッド: どこでもよい(const の関数は中身を書き換えないので、別々のスレッドから同時に呼んでもよい)
#pragma once

#include <cstdint>
#include <memory> // std::unique_ptr
#include <string>
#include <vector>

namespace husky::engine
{
    // 1つの字の絵と置き方。
    struct GlyphBitmap
    {
        int width = 0;  // 絵の大きさ(ピクセル)。空白のように描くものが無い字は 0
        int height = 0;
        int offsetX = 0; // ペンの位置から絵の左上までのずれ(右が +x、下が +y)
        int offsetY = 0;
        float advance = 0.0f; // 次の字のペンの位置まで、右へ進む距離
        // 濃さ(0 = 透明、255 = 塗りつぶし)。width × height 個、左上から1行ずつ。
        std::vector<std::uint8_t> coverage;
    };

    // 行の高さに関わる値(ピクセル)。どれも 0 以上。行送り(次の行のベースラインまで)は ascent + descent + lineGap。
    struct FontLineMetrics
    {
        float ascent = 0.0f;  // ベースラインから字のいちばん上まで
        float descent = 0.0f; // ベースラインから字のいちばん下まで(下へ向かう距離を正の数で)
        float lineGap = 0.0f; // 行と行の間に空ける量
    };

    class FontFace
    {
    public:
        virtual ~FontFace() = default; // 基底クラスのポインタで壊すので、デストラクタを仮想にする

        // pixelHeight: 字の大きさ。フォントのいちばん上(ascent)からいちばん下(descent)までがこのピクセル数になる。
        // (ImGui などと同じ決まり。漢字そのものは、これより少し小さく描かれる)

        // このフォントに codepoint(Unicode の番号。'A' = 0x41、'あ' = 0x3042)の字があるか。
        virtual bool HasGlyph(char32_t codepoint) const = 0;

        // 字の絵を作って out に入れる(out の coverage は使い回すので、毎回の確保を減らせる)。
        // 戻り値: 字が無ければ false(out は空にする)。空白のように絵が無い字は true で、大きさ 0・advance だけ。
        virtual bool RenderGlyph(char32_t codepoint, float pixelHeight, GlyphBitmap& out) const = 0;

        virtual FontLineMetrics GetLineMetrics(float pixelHeight) const = 0;

        // left の次に right を並べるときの、advance に足す詰め(カーニング。ピクセル。詰めるときはマイナス)。
        // 情報が無いフォントや字の組なら 0。
        virtual float GetKerning(char32_t left, char32_t right, float pixelHeight) const = 0;
    };

    // TTF(か OTF)のファイルの中身(バイト列。platform::ReadTextFile で読んだまま)からフォントを作る。
    // 中身は返したフォントが持つ(std::move で渡せば写さない)。読めないファイルなら nullptr。
    std::unique_ptr<FontFace> CreateFontFace(std::string fileData);
}
