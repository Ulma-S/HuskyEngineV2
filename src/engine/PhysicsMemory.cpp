// PhysicsMemory.h の実装。Box2D のヘッダを読むのは、物理の中身の .cpp だけ(ここもその1つ)。
#include "engine/PhysicsMemory.h"

#include "core/MemoryTracker.h"

#include <box2d/box2d.h>

namespace husky::engine
{
    namespace
    {
        // Box2D の確保の関数の形(b2AllocFcn): 大きさと、そろえる単位(alignment)を受け取る。
        void* AllocateForBox2D(unsigned int size, int alignment)
        {
            return core::TrackedAllocate(static_cast<std::size_t>(size), static_cast<std::size_t>(alignment));
        }

        // Box2D の解放の関数の形(b2FreeFcn)。
        void FreeForBox2D(void* memory)
        {
            core::TrackedFree(memory);
        }
    }

    void InstallPhysicsMemoryHooks()
    {
        // 関数のポインタを渡すだけ(C の関数のポインタ。取り込みの無い関数なので、そのまま渡せる)
        b2SetAllocator(AllocateForBox2D, FreeForBox2D);
    }

    std::size_t GetPhysicsAllocatedBytes()
    {
        const int bytes = b2GetByteCount();
        return bytes > 0 ? static_cast<std::size_t>(bytes) : 0;
    }
}
