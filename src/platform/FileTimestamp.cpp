// FileTimestamp の中身。
#include "platform/FileTimestamp.h"

#include <filesystem>
#include <system_error>

namespace husky::platform
{
    struct FileTimestamp::Impl
    {
        std::filesystem::path path;
    };

    FileTimestamp::FileTimestamp() : m_impl(std::make_unique<Impl>()) {}

    FileTimestamp::~FileTimestamp() = default;

    void FileTimestamp::SetPath(const std::string& utf8Path)
    {
        m_utf8Path = utf8Path;
        // u8path: 文字列を UTF-8 として読む(ふつうの path(std::string) は、Windows では OS の既定の文字コードとして読むので、
        // 日本語のフォルダ名が化ける)。C++20 では非推奨だが、このプロジェクトは C++17。
        m_impl->path = std::filesystem::u8path(utf8Path);
    }

    bool FileTimestamp::Read(std::int64_t& out) const
    {
        // 例外を使わない版(error_code を受け取る)を呼ぶ。このプロジェクトは例外を無効にしている。
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(m_impl->path, error);
        if (error)
        {
            return false;
        }
        out = static_cast<std::int64_t>(time.time_since_epoch().count());
        return true;
    }
}
