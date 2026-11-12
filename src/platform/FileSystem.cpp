// FileSystem.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/FileSystem.h"

#include <SDL3/SDL.h>

namespace husky::platform
{
    std::string GetExecutableDirectory()
    {
        // SDL_GetBasePath が返す文字列は SDL が持っているので、解放しなくてよい。
        const char* basePath = SDL_GetBasePath();
        return basePath != nullptr ? std::string(basePath) : std::string();
    }

    bool ReadTextFile(const char* path, std::string& outText)
    {
        // SDL_LoadFile: ファイル全体を読み、SDL が確保したメモリに入れて返す。使い終わったら SDL_free で返す。
        std::size_t size = 0;
        void* data = SDL_LoadFile(path, &size);
        if (data == nullptr)
        {
            SDL_Log("ファイルを読めませんでした (%s): %s", path, SDL_GetError());
            return false;
        }
        outText.assign(static_cast<const char*>(data), size);
        SDL_free(data);
        return true;
    }

    bool WriteTextFile(const char* path, std::string_view text)
    {
        // 1. 一時ファイルに書く。
        const std::string temporaryPath = std::string(path) + ".tmp";
        if (!SDL_SaveFile(temporaryPath.c_str(), text.data(), text.size()))
        {
            SDL_Log("ファイルを書けませんでした (%s): %s", temporaryPath.c_str(), SDL_GetError());
            return false;
        }
        // 2. 書き終えたら、名前を変えて置き換える。名前の変更は一瞬で終わるので、
        //    「途中まで書かれた壊れたファイル」が path に残ることはない(Windows では MoveFileExW で置き換える)。
        if (!SDL_RenamePath(temporaryPath.c_str(), path))
        {
            SDL_Log("ファイルを置き換えられませんでした (%s → %s): %s", temporaryPath.c_str(), path, SDL_GetError());
            SDL_RemovePath(temporaryPath.c_str()); // 一時ファイルを残さない
            return false;
        }
        return true;
    }

    bool FileExists(const char* path)
    {
        // SDL_GetPathInfo は、パスがあれば true を返す(種類や大きさも取れるが、ここでは使わない)。
        SDL_PathInfo info;
        return SDL_GetPathInfo(path, &info);
    }

    bool RemoveFile(const char* path)
    {
        return SDL_RemovePath(path);
    }
}
