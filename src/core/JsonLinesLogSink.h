// ログを JSON Lines(1行が1件の JSON)で書き出す受け手(LOG-3)。ファイルに残し、落ちた後でも読め、将来のビュワーも読む。
//
// 仕組み:
//   - Receive(ログを出したスレッドで呼ばれる)は、記録を決まった大きさの入れ物(リングバッファ)に写すだけ。
//     文字にする・ファイルに書く重い仕事は、この受け手が持つ書き出し用のスレッドが行う(ゲームスレッドを待たせない)。
//   - 入れ物がいっぱいなら、その記録は捨てて数える。次に書けたときに "dropped" の行を書く(黙って消さない)。
//   - 書き出し先は LogLineWriter(core はファイルを知らない。ファイルは platform の LogFileWriter)。
//   - 入れ物は作るときに1回だけ確保する。そのあとは確保しない。
//
// スレッド: Receive はどのスレッドからでも(Log の mutex の中)。書き出しのスレッドは Log の mutex を取ってから
// 名前を引くが、この受け手の mutex を持ったままにはしないので、2つの mutex を逆の順に取って止まることは無い。
#pragma once
#include "core/Log.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace husky::core
{
    // 1行を書く先(ファイル、テスト用の文字列など)。書き出しのスレッドだけが呼ぶ。
    class LogLineWriter
    {
    public:
        virtual ~LogLineWriter() = default;
        // 1行を書く(text に改行は入っていない。書く側が行の終わりを付ける)。
        virtual void WriteLine(const char* text, std::size_t length) = 0;
        // 溜まっているものを書き出す(入れ物が空になるたびに呼ぶ。落ちる直前の行も残るように)。
        virtual void Flush() {}
    };

    class JsonLinesLogSink : public LogSink
    {
    public:
        // capacity: 書き出しを待てる記録の数。既定は Verbose から全部(絞り込みは GetFilter)。
        explicit JsonLinesLogSink(LogLineWriter& writer, std::size_t capacity = 1024,
                                  LogLevel minimum = LogLevel::Verbose);
        // 残りを全部書いてから、書き出しのスレッドを止める。
        ~JsonLinesLogSink() override;

        JsonLinesLogSink(const JsonLinesLogSink&) = delete;
        JsonLinesLogSink& operator=(const JsonLinesLogSink&) = delete;

        void Receive(const LogRecord& record) override;

        // ここまでに受け取った記録が全部書かれるまで待つ(テストと、終わる前)。Log の mutex を持ったまま呼ばない。
        void Flush();

        LogChannelFilter& GetFilter() { return m_filter; }
        std::uint64_t GetDroppedCount();

    private:
        void Run(); // 書き出しのスレッド

        LogLineWriter& m_writer;
        LogChannelFilter m_filter;

        std::mutex m_mutex;
        std::condition_variable m_wake; // 書くものが来た・止める
        std::condition_variable m_idle; // 入れ物が空になった(Flush が待つ)
        std::vector<LogRecord> m_ring;  // リングバッファ(m_head から m_count 個)
        std::size_t m_head = 0;
        std::size_t m_count = 0;
        bool m_writing = false; // 書き出しのスレッドが1件を書いている途中
        bool m_stop = false;
        std::uint64_t m_dropped = 0;         // 捨てた数(合計)
        std::uint64_t m_droppedReported = 0; // そのうち "dropped" の行に書いた数

        std::thread m_thread; // 最後に作る(ほかのメンバーができてから動き出すように)
    };
}
