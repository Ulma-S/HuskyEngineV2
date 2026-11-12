// MemoryHooks.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/MemoryHooks.h"

#include "core/MemoryTracker.h"

#include <SDL3/SDL.h>

#include <cstddef>

namespace husky::platform
{
    namespace
    {
        bool g_installed = false;

        // SDL に渡す4つの関数。SDLCALL は「SDL が決めた呼び出しの約束」(引数の渡し方)に合わせる印。
        // SDL は 0 バイトの確保を 1 バイトに直してから呼ぶので、ここでは 0 を気にしなくてよい。
        void* SDLCALL TrackedSdlMalloc(std::size_t size)
        {
            return core::TrackedAllocate(size, 16);
        }
        void* SDLCALL TrackedSdlCalloc(std::size_t count, std::size_t size)
        {
            return core::TrackedAllocateZeroed(count, size);
        }
        void* SDLCALL TrackedSdlRealloc(void* pointer, std::size_t size)
        {
            return core::TrackedReallocate(pointer, size);
        }
        void SDLCALL TrackedSdlFree(void* pointer)
        {
            core::TrackedFree(pointer);
        }
    }

    bool InstallMemoryHooks()
    {
        if (g_installed)
        {
            return true;
        }
        if (!SDL_SetMemoryFunctions(TrackedSdlMalloc, TrackedSdlCalloc, TrackedSdlRealloc, TrackedSdlFree))
        {
            SDL_Log("SDL_SetMemoryFunctions に失敗しました: %s", SDL_GetError());
            return false;
        }
        g_installed = true;
        return true;
    }

    bool AreMemoryHooksInstalled()
    {
        return g_installed;
    }
}
