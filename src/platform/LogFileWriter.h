// ログの行をファイルに書く(core::LogLineWriter の、ファイル版)。JsonLinesLogSink に渡して使う(LOG-3)。
//   platform::LogFileWriter file;
//   if (file.Open(path)) { core::JsonLinesLogSink sink(file); ... }
// 開くときに中身を空にする(起動のたびに上書き)。
//
// 動作スレッド: Open と破棄は1つのスレッド(main)。WriteLine / Flush は受け手の書き出しのスレッドだけが呼ぶ。
#pragma once
#include "core/JsonLinesLogSink.h"

namespace husky::platform
{
    class LogFileWriter : public core::LogLineWriter
    {
    public:
        LogFileWriter() = default;
        ~LogFileWriter() override;
        LogFileWriter(const LogFileWriter&) = delete;
        LogFileWriter& operator=(const LogFileWriter&) = delete;

        // ファイルを開く(無ければ作り、あれば空にする。フォルダは先に作っておくこと)。失敗したら false。
        bool Open(const char* path);

        void WriteLine(const char* text, std::size_t length) override;
        void Flush() override;

    private:
        void* m_stream = nullptr; // 中身は SDL_IOStream*(ヘッダに SDL の型を出さないため void*)
    };
}
