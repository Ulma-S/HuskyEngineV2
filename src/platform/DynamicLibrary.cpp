// DynamicLibrary の中身。SDL の読み込みの関数を使う(OS ごとの違いは SDL が吸収する)。
#include "platform/DynamicLibrary.h"

#include "platform/PlatformLog.h"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_error.h>

namespace husky::platform
{
    DynamicLibrary::~DynamicLibrary()
    {
        Unload();
    }

    bool DynamicLibrary::Load(const char* path)
    {
        Unload();
        SDL_SharedObject* object = SDL_LoadObject(path);
        if (object == nullptr)
        {
            HUSKY_LOG(PlatformLog::LibraryLoadFailed, path, SDL_GetError());
            return false;
        }
        m_handle = object;
        m_path = path;
        return true;
    }

    void* DynamicLibrary::FindFunction(const char* name) const
    {
        if (m_handle == nullptr)
        {
            return nullptr;
        }
        // SDL_FunctionPointer は「引数なし・戻り値なしの関数のポインタ」。使う側で正しい型に変える。
        SDL_FunctionPointer function = SDL_LoadFunction(static_cast<SDL_SharedObject*>(m_handle), name);
        if (function == nullptr)
        {
            HUSKY_LOG(PlatformLog::LibraryFunctionMissing, m_path, name);
            return nullptr;
        }
        return reinterpret_cast<void*>(function);
    }

    void DynamicLibrary::Unload()
    {
        if (m_handle != nullptr)
        {
            SDL_UnloadObject(static_cast<SDL_SharedObject*>(m_handle));
            m_handle = nullptr;
            m_path.clear();
        }
    }

    const char* DynamicLibrary::GetFileSuffix()
    {
#if defined(_WIN32)
        return ".dll";
#elif defined(__APPLE__)
        return ".dylib";
#else
        return ".so";
#endif
    }
}
