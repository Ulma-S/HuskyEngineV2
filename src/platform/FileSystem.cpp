// FileSystem.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/FileSystem.h"
#include "platform/PlatformLog.h"

#include <SDL3/SDL.h>

#include <algorithm> // std::sort, std::replace
#include <utility>   // std::move

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
            HUSKY_LOG(PlatformLog::FileReadFailed, path, SDL_GetError());
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
            HUSKY_LOG(PlatformLog::FileWriteFailed, temporaryPath, SDL_GetError());
            return false;
        }
        // 2. 書き終えたら、名前を変えて置き換える。名前の変更は一瞬で終わるので、
        //    「途中まで書かれた壊れたファイル」が path に残ることはない(Windows では MoveFileExW で置き換える)。
        if (!SDL_RenamePath(temporaryPath.c_str(), path))
        {
            HUSKY_LOG(PlatformLog::FileReplaceFailed, temporaryPath, path, SDL_GetError());
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

    bool DuplicateFile(const char* from, const char* to)
    {
        if (!SDL_CopyFile(from, to))
        {
            HUSKY_LOG(PlatformLog::FileCopyFailed, from, to, SDL_GetError());
            return false;
        }
        return true;
    }

    bool MakeDirectory(const char* path)
    {
        // SDL_CreateDirectory は、途中のフォルダもまとめて作り、すでにあるときも true を返す。
        if (!SDL_CreateDirectory(path))
        {
            HUSKY_LOG(PlatformLog::DirectoryCreateFailed, path, SDL_GetError());
            return false;
        }
        return true;
    }

    bool ListFilesRecursive(const char* directory, std::vector<std::string>& outPaths)
    {
        outPaths.clear();
        // SDL_GlobDirectory: フォルダの中を、下のフォルダまでたどって一覧にする。
        // 第2引数(絞り込みの模様)が nullptr なら、すべてを返す。返すパスは directory からの相対で、区切りは「/」。
        // 返した配列は1つの確保にまとまっているので、使い終わったら SDL_free で1回返す。
        int count = 0;
        char** entries = SDL_GlobDirectory(directory, nullptr, 0, &count);
        if (entries == nullptr)
        {
            HUSKY_LOG(PlatformLog::DirectoryReadFailed, directory, SDL_GetError());
            return false;
        }

        std::string base = directory;
        if (!base.empty() && base.back() != '/' && base.back() != '\\')
        {
            base += '/';
        }
        for (int i = 0; i < count; ++i)
        {
            // フォルダも一覧に入ってくるので、ファイルだけを残す。
            SDL_PathInfo info;
            const std::string fullPath = base + entries[i];
            if (SDL_GetPathInfo(fullPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
            {
                // SDL の説明では区切りは「/」だが、Windows では「\」で返ってくる(テストで分かった)。
                // このヘッダは「区切りは /」と約束しているので、OS による違いは、ここ(platform)で吸収する。
                std::string path = entries[i];
                std::replace(path.begin(), path.end(), '\\', '/');
                outPaths.push_back(std::move(path));
            }
        }
        SDL_free(entries);

        std::sort(outPaths.begin(), outPaths.end());
        return true;
    }
}
