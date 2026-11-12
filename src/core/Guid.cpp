// Guid.h の実装。
#include "core/Guid.h"

#include <random> // std::random_device(OS の乱数)

namespace husky::core
{
    namespace
    {
        // 16 進数の1文字を、0〜15 の数にする。16 進数の文字でなければ -1。
        int HexValue(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        }

        // "8-4-4-4-12" の、ハイフンが来る位置(0 から数えた文字の位置)。
        bool IsHyphenPosition(std::size_t index)
        {
            return index == 8 || index == 13 || index == 18 || index == 23;
        }

        constexpr std::size_t TextLength = 36; // 32 文字の 16 進数 + 4 つのハイフン
    }

    Guid Guid::Generate()
    {
        // std::random_device は、OS が用意している乱数(Windows では暗号にも使える質のもの)を返す。
        // 毎回 OS に聞くので少し遅いが、GUID を作るのはアセットを作るときくらいなので問題ない。
        // static にはしない(複数のスレッドから同時に呼んでも、それぞれ自分の random_device を使うので安全)。
        std::random_device random;
        auto next64 = [&random]() {
            // random_device は 32 ビットずつ返すので、2回分をつなげて 64 ビットにする。
            const std::uint64_t upper = random();
            const std::uint64_t lower = random();
            return (upper << 32) | lower;
        };

        Guid guid;
        guid.high = next64();
        guid.low = next64();
        // UUID のバージョン 4(乱数で作ったもの)の印: 13 文字目の 16 進数を 4 にする。
        guid.high = (guid.high & ~0x000000000000F000ull) | 0x0000000000004000ull;
        // バリアント(UUID の種類)の印: 17 文字目の 16 進数の上位2ビットを 10 にする(8、9、a、b のどれかになる)。
        guid.low = (guid.low & ~0xC000000000000000ull) | 0x8000000000000000ull;
        return guid;
    }

    std::string Guid::ToString() const
    {
        static constexpr char Digits[] = "0123456789abcdef";
        std::string text;
        text.reserve(TextLength);
        // 上の桁から順に、4 ビット(16 進数の1文字)ずつ取り出す。
        auto appendHex = [&text](std::uint64_t value) {
            for (int shift = 60; shift >= 0; shift -= 4)
            {
                if (IsHyphenPosition(text.size()))
                {
                    text.push_back('-');
                }
                text.push_back(Digits[(value >> shift) & 0xF]);
            }
        };
        appendHex(high);
        appendHex(low);
        return text;
    }

    bool Guid::Parse(std::string_view text, Guid& out)
    {
        if (text.size() != TextLength)
        {
            return false;
        }
        Guid result;
        int digitCount = 0; // 読んだ 16 進数の文字の数(0〜31)
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (IsHyphenPosition(i))
            {
                if (text[i] != '-')
                {
                    return false;
                }
                continue;
            }
            const int value = HexValue(text[i]);
            if (value < 0)
            {
                return false;
            }
            // 前半の 16 文字は high に、後半の 16 文字は low に、上の桁から詰めていく。
            std::uint64_t& target = digitCount < 16 ? result.high : result.low;
            target = (target << 4) | static_cast<std::uint64_t>(value);
            ++digitCount;
        }
        out = result;
        return true;
    }
}
