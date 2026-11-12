// AddressSanitizer(ASan)に、自作アロケータの中の「使ってよい場所 / いけない場所」を教えるための道具。
//
// ASan が知っているのは、標準の new / malloc で借りて返した範囲だけ。
// 自作アロケータは大きな塊を1回だけ借りて中を切り分けるので、ASan からは塊全体が「使用中」に見え、
// 中で返したメモリに触っても気づけない。そこで、アロケータが自分で印を付ける(ポイズニング)。
//   - HUSKY_ASAN_POISON(p, n)  : p から n バイトを「触ったらエラー」にする
//   - HUSKY_ASAN_UNPOISON(p, n): p から n バイトを「触ってよい」に戻す
//
// ASan を使わないビルドでは、どれも何もしない(速さは変わらない)。
// ASan のビルドかどうかは、コンパイラが自動で決める記号(マクロ)で見分ける:
//   MSVC と GCC は /fsanitize=address のときに __SANITIZE_ADDRESS__ を定義する。Clang は __has_feature で調べる。
#pragma once

#if defined(__SANITIZE_ADDRESS__)
#define HUSKY_ASAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HUSKY_ASAN_ENABLED 1
#endif
#endif

#if defined(HUSKY_ASAN_ENABLED)

#include <sanitizer/asan_interface.h>

#define HUSKY_ASAN_POISON(address, size) ASAN_POISON_MEMORY_REGION((address), (size))
#define HUSKY_ASAN_UNPOISON(address, size) ASAN_UNPOISON_MEMORY_REGION((address), (size))

// 関数に付けると、その関数の中の読み書きは ASan が確認しない(確認のコードを差し込まない)。
// アロケータ自身は、印を付けた場所(ヘッダや空きブロック)を読み書きする必要があるので、これを付ける。
#if defined(_MSC_VER) && !defined(__clang__)
#define HUSKY_NO_SANITIZE_ADDRESS __declspec(no_sanitize_address)
#else
#define HUSKY_NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address")))
#endif

#else

// ASan を使わないビルドでは何もしない。(void)(x) は「x を使った」ことにして、未使用の警告を消す書き方。
#define HUSKY_ASAN_POISON(address, size) ((void)(address), (void)(size))
#define HUSKY_ASAN_UNPOISON(address, size) ((void)(address), (void)(size))
#define HUSKY_NO_SANITIZE_ADDRESS

#endif

namespace husky::core
{
    // ASan 付きでビルドされていれば true。テストで「ASan のビルドでだけ確かめる」ときに使う。
    constexpr bool IsAddressSanitizerEnabled()
    {
#if defined(HUSKY_ASAN_ENABLED)
        return true;
#else
        return false;
#endif
    }

    // address の1バイトに「触ったらエラー」の印が付いていれば true(ASan のビルドでだけ意味がある。それ以外は常に false)。
    // 印を確かめるだけで、触らないので、ASan のエラーにはならない。テスト用。
    inline bool IsPoisonedByAddressSanitizer(const void* address)
    {
#if defined(HUSKY_ASAN_ENABLED)
        return __asan_address_is_poisoned(address) != 0;
#else
        (void)address;
        return false;
#endif
    }
}
