// UTF-8 の文字列から、字(Unicode の番号)を1つずつ取り出す(UI-3b。文字を並べる UI-4 でも使う)。
// UTF-8 は1つの字を 1〜4 バイトで書く: 英数字は 1 バイト、ひらがな・漢字は 3 バイト、絵文字は 4 バイト。
// 先頭のバイトの上のビットで、その字が何バイトかが分かる(0xxxxxxx = 1、110xxxxx = 2、1110xxxx = 3、11110xxx = 4)。
// 動作スレッド: どこでもよい
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace husky::engine
{
    // 壊れた並び(途中で切れている、続きのバイトの形が違う、無駄に長い書き方など)の代わりに返す字(�)。
    constexpr char32_t ReplacementCharacter = U'�';

    // text の index バイト目から字を1つ読み、index を次の字の先頭へ進める。index が終わりなら false。
    // 壊れた並びは ReplacementCharacter を1つ返し、1 バイトだけ進める(読み続けられるように)。
    inline bool NextCodepoint(std::string_view text, std::size_t& index, char32_t& outCodepoint)
    {
        if (index >= text.size())
        {
            return false;
        }
        const auto byteAt = [&text](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
        const std::uint8_t first = byteAt(index);
        int length = 0;
        char32_t value = 0;
        char32_t minimum = 0; // この長さで書くべき最小の値(これより小さい値は短く書けるので、壊れた並びとみなす)
        if (first < 0x80)
        {
            outCodepoint = first;
            ++index;
            return true;
        }
        if ((first & 0xE0) == 0xC0)
        {
            length = 2;
            value = first & 0x1F;
            minimum = 0x80;
        }
        else if ((first & 0xF0) == 0xE0)
        {
            length = 3;
            value = first & 0x0F;
            minimum = 0x800;
        }
        else if ((first & 0xF8) == 0xF0)
        {
            length = 4;
            value = first & 0x07;
            minimum = 0x10000;
        }
        else
        {
            outCodepoint = ReplacementCharacter; // 続きのバイト(10xxxxxx)や、使わない形から始まっている
            ++index;
            return true;
        }
        if (index + static_cast<std::size_t>(length) > text.size())
        {
            outCodepoint = ReplacementCharacter; // 途中で切れている
            ++index;
            return true;
        }
        for (int i = 1; i < length; ++i)
        {
            const std::uint8_t next = byteAt(index + static_cast<std::size_t>(i));
            if ((next & 0xC0) != 0x80)
            {
                outCodepoint = ReplacementCharacter; // 続きのバイトの形でない
                ++index;
                return true;
            }
            value = (value << 6) | (next & 0x3F); // 続きのバイトは下の 6 ビットずつ
        }
        // 無駄に長い書き方、Unicode の外(0x10FFFF より大きい)、サロゲート(UTF-16 の部品で、字ではない)は壊れた並び
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        {
            outCodepoint = ReplacementCharacter;
            ++index;
            return true;
        }
        outCodepoint = value;
        index += static_cast<std::size_t>(length);
        return true;
    }
}
