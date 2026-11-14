// GUID(世界中で重ならない 128 ビットの ID)。アセットを指す、変わらない ID に使う。
//
// ファイルの名前や場所は変わることがあるが、GUID は一度振ったら変えない。
// そのため、シーンのファイルなどにはアセットのパスではなく GUID を書いておくと、ファイルを動かしても参照が切れない。
// (Unity の .meta ファイルの guid と同じ考え方)
//
// 文字列の形は、よく使われる「8-4-4-4-12」の 16 進数(小文字):
//   "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43"
//
// 使い方:
//   Guid id = Guid::Generate();                  // 新しく作る(乱数。ほかと重ならない)
//   std::string text = id.ToString();            // 文字列にする(ファイルに書くとき)
//   Guid loaded;
//   if (Guid::Parse(text, loaded)) { ... }       // 文字列から読む(ファイルから読むとき)
//
// 動作スレッド: どこでもよい(Generate も、複数のスレッドから同時に呼んでよい)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional> // std::hash
#include <string>
#include <string_view>

namespace husky::core
{
    struct Guid
    {
        // 128 ビットを、64 ビットの整数2つで持つ(文字列の前半が high、後半が low)。
        std::uint64_t high = 0;
        std::uint64_t low = 0;

        // すべて 0 の GUID は「どこも指さない」を表す(ハンドルの IsNull と同じ考え方)。
        bool IsNull() const { return high == 0 && low == 0; }

        bool operator==(const Guid& other) const { return high == other.high && low == other.low; }
        bool operator!=(const Guid& other) const { return !(*this == other); }
        // 並べ替え(std::sort や std::map の鍵)に使えるように、大小も決めておく。
        bool operator<(const Guid& other) const { return high != other.high ? high < other.high : low < other.low; }

        // 新しい GUID を作る。OS の乱数(暗号にも使える質のもの)から作るので、ほかの GUID と重なる心配はまず無い。
        // 形式は「バージョン 4(乱数)」の UUID(決まった位置の数ビットに、その印を書く)。
        static Guid Generate();

        // 2つの GUID から、いつも同じ新しい GUID を作る(入れ子のプレハブで、外のプレハブの中の ID と中のプレハブの ID から、
        // 外のプレハブでの ID を決めるため。HIER)。順番が違えば別の値。形式はバージョン 4 の印を付ける。
        static Guid Combine(const Guid& a, const Guid& b);

        // "8-4-4-4-12" の小文字の 16 進数にする。
        std::string ToString() const;

        // 文字列から読む。大文字の 16 進数も受け付ける。形が違えば false を返し、out は変えない。
        static bool Parse(std::string_view text, Guid& out);
    };
}

// std::hash の特殊化: Guid を std::unordered_map(C# の Dictionary)の鍵に使えるようにする。
// 標準ライブラリのテンプレートに、自分の型の分を足す書き方(std の中に書いてよい、数少ない例外)。
template <>
struct std::hash<husky::core::Guid>
{
    std::size_t operator()(const husky::core::Guid& guid) const noexcept
    {
        // GUID はもともと乱数なので、2つを混ぜるだけで十分ばらける。
        return static_cast<std::size_t>(guid.high ^ (guid.low * 0x9E3779B97F4A7C15ull));
    }
};
