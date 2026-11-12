// プログラム全体の operator new / delete を差し替えて、すべてを MemoryTracker(エンジンの入口)に通す。
//
// C++ では、グローバルな operator new / delete を自分で定義すると、標準ライブラリのものの代わりに使われる(差し替え)。
// new、std::make_unique、std::vector などのコンテナは、最後は operator new / delete を呼ぶので、
// 何も書き換えなくても、ここを通って数えられる。
//
// このファイルは husky_core のライブラリには入れず、exe(ゲームとテスト)に直接入れる(CMakeLists.txt の HUSKY_GLOBAL_NEW_SOURCE)。
//   - 静的ライブラリの中に置くと、リンカが標準のものを先に選び、差し替えが効かないことがあるため。
//   - 差し替えはプログラム全体に1つだけ。exe ごとに1回入れる。
//
// 動作スレッド: どこでもよい。
#include "core/MemoryTracker.h"

#include <cstddef>
#include <cstdlib> // std::abort
#include <new>     // std::align_val_t, std::nothrow_t

namespace
{
    // 例外を使わないので、メモリが足りないときは、標準の「std::bad_alloc を投げる」の代わりに、プログラムを止める。
    // (new が nullptr を返すと、呼んだ側はそれを確かめないので、もっと分かりにくい壊れ方をする)
    void* AllocateOrAbort(std::size_t size, std::size_t alignment)
    {
        void* pointer = husky::core::TrackedAllocate(size, alignment);
        if (pointer == nullptr)
        {
            std::abort();
        }
        return pointer;
    }

    // alignas で大きなアラインメントを指定した型のとき、C++17 からは std::align_val_t 付きの new が呼ばれる。
    std::size_t ToSize(std::align_val_t alignment)
    {
        return static_cast<std::size_t>(alignment);
    }
}

// --- 確保 ---
// 通常の new。__STDCPP_DEFAULT_NEW_ALIGNMENT__ は、何も指定しない new が保証するアラインメント(MSVC の x64 では 16)。
void* operator new(std::size_t size) { return AllocateOrAbort(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new[](std::size_t size) { return AllocateOrAbort(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
// new (std::nothrow) T: 失敗したら止めずに nullptr を返す版。
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return husky::core::TrackedAllocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return husky::core::TrackedAllocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
// 大きなアラインメントの型の new。
void* operator new(std::size_t size, std::align_val_t alignment) { return AllocateOrAbort(size, ToSize(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return AllocateOrAbort(size, ToSize(alignment)); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return husky::core::TrackedAllocate(size, ToSize(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return husky::core::TrackedAllocate(size, ToSize(alignment));
}

// --- 解放 ---
// どの new で確保したものも、TrackedFree で返せる(大きさとアラインメントは見出しに書いてあるので、引数は使わない)。
void operator delete(void* pointer) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer) noexcept { husky::core::TrackedFree(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { husky::core::TrackedFree(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { husky::core::TrackedFree(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { husky::core::TrackedFree(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { husky::core::TrackedFree(pointer); }
