// LogFileWriter.h の実装。SDL の IOStream でファイルに書く。
#include "platform/LogFileWriter.h"

#include <SDL3/SDL.h>

namespace husky::platform
{
    LogFileWriter::~LogFileWriter()
    {
        if (m_stream != nullptr)
            SDL_CloseIO(static_cast<SDL_IOStream*>(m_stream));
    }

    bool LogFileWriter::Open(const char* path)
    {
        if (m_stream != nullptr)
            return false;
        // "wb": 書くために開き、中身を空にする(b は改行を書き換えない。Windows でも \n のまま)。
        // ここではログを出さない(ログのファイルを開けないことをログのファイルに書いても届かない。呼ぶ側が画面に出す)。
        m_stream = SDL_IOFromFile(path, "wb");
        return m_stream != nullptr;
    }

    void LogFileWriter::WriteLine(const char* text, std::size_t length)
    {
        if (m_stream == nullptr)
            return;
        SDL_IOStream* stream = static_cast<SDL_IOStream*>(m_stream);
        SDL_WriteIO(stream, text, length);
        SDL_WriteIO(stream, "\n", 1);
    }

    void LogFileWriter::Flush()
    {
        if (m_stream != nullptr)
            SDL_FlushIO(static_cast<SDL_IOStream*>(m_stream));
    }
}
