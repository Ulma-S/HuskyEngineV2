// JSON Lines の受け手(LOG-3)。仕組みは JsonLinesLogSink.h の最初のコメント。
#include "core/JsonLinesLogSink.h"

#include <cstdio>

namespace husky::core
{
    JsonLinesLogSink::JsonLinesLogSink(LogLineWriter& writer, std::size_t capacity, LogLevel minimum)
        : m_writer(writer)
        , m_filter(minimum)
        , m_ring(capacity > 0 ? capacity : 1) // ここで1回だけ確保する
        , m_thread(&JsonLinesLogSink::Run, this)
    {
    }

    JsonLinesLogSink::~JsonLinesLogSink()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_one();
        m_thread.join(); // 書き出しのスレッドは、残りを書き終えてから抜ける
    }

    void JsonLinesLogSink::Receive(const LogRecord& record)
    {
        if (!m_filter.Passes(record))
            return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_count == m_ring.size())
            {
                ++m_dropped; // いっぱい: 捨てて数える(ログを出した側を待たせない)
                return;
            }
            m_ring[(m_head + m_count) % m_ring.size()] = record;
            ++m_count;
        }
        m_wake.notify_one();
    }

    void JsonLinesLogSink::Flush()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_idle.wait(lock, [this] { return m_count == 0 && !m_writing; });
    }

    std::uint64_t JsonLinesLogSink::GetDroppedCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

    void JsonLinesLogSink::Run()
    {
        // 書き出しのスレッドだけが使う入れ物(スタックに置く。確保しない)。
        LogRecord record;
        char line[8192];

        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;)
        {
            m_wake.wait(lock, [this] { return m_count > 0 || m_stop || m_dropped > m_droppedReported; });
            if (m_count == 0 && m_dropped == m_droppedReported)
            {
                if (m_stop)
                    break;
                continue;
            }

            // 1件を取り出し、mutex を放してから書く(書いている間も、ほかのスレッドは Receive できる)。
            const std::uint64_t droppedNow = m_dropped;
            const bool hasRecord = m_count > 0;
            if (hasRecord)
            {
                record = m_ring[m_head];
                m_head = (m_head + 1) % m_ring.size();
                --m_count;
            }
            m_writing = true;
            lock.unlock();

            if (droppedNow > m_droppedReported)
            {
                // 捨てた記録があったことを書く(m_droppedReported はこのスレッドだけが書く)。
                const int length = std::snprintf(line, sizeof(line),
                                                 "{\"level\":\"Warning\",\"channel\":\"Engine.Log\",\"dropped\":%llu}",
                                                 static_cast<unsigned long long>(droppedNow - m_droppedReported));
                m_writer.WriteLine(line, static_cast<std::size_t>(length));
            }
            if (hasRecord)
            {
                const std::size_t length = Log::Get().FormatRecordJson(record, line, sizeof(line));
                m_writer.WriteLine(line, length);
            }

            lock.lock();
            m_droppedReported = droppedNow;
            if (m_count == 0)
            {
                // 入れ物が空になった: 書き出し先に溜まっているものを書き出してから、Flush を待っている人を起こす。
                lock.unlock();
                m_writer.Flush();
                lock.lock();
            }
            m_writing = false;
            if (m_count == 0)
                m_idle.notify_all();
        }
        m_idle.notify_all();
    }
}
