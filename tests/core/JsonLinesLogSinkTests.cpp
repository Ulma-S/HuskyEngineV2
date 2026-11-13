// JSON Lines の受け手(core/JsonLinesLogSink.h)と、記録を JSON の1行にする Log::FormatRecordJson のテスト。
// 書き出し先は、行を貯めるだけのテスト用のもの。書き出しのスレッドを待つのは Flush。
#include "core/JsonLinesLogSink.h"

#include <doctest/doctest.h>

#include <mutex>
#include <string>
#include <vector>

using husky::core::JsonLinesLogSink;
using husky::core::Log;
using husky::core::LogLevel;
using husky::core::LogLineWriter;
using husky::core::ScopedLogSink;

namespace jsonlinestest
{
    enum class JsonTestLog : std::uint16_t
    {
        Loaded = 1,
        Rate = 2,
    };

    HUSKY_LOG_CHANNEL_BEGIN(JsonTestLog, "JsonTest.Sink")
        HUSKY_LOG_MESSAGE(Loaded, Warning, "{path} を読みました({count} 個)", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(count, Int))
        HUSKY_LOG_MESSAGE(Rate, Verbose, "{rate} {ok}", HUSKY_LOG_ARG(rate, Float), HUSKY_LOG_ARG(ok, Bool))
    HUSKY_LOG_CHANNEL_END()
}

namespace
{
    // 書かれた行を貯めるだけの書き出し先(書き出しのスレッドから呼ばれるので mutex で守る)。
    class MemoryLineWriter : public LogLineWriter
    {
    public:
        void WriteLine(const char* text, std::size_t length) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            lines.emplace_back(text, length);
        }
        void Flush() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++flushCount;
        }
        std::vector<std::string> Lines()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return lines;
        }

        std::mutex mutex;
        std::vector<std::string> lines;
        int flushCount = 0;
    };

    bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
}

TEST_CASE("JsonLinesLogSink は記録を1行の JSON にして、書き出しのスレッドで書く")
{
    MemoryLineWriter writer;
    {
        JsonLinesLogSink sink(writer);
        ScopedLogSink attach(sink);
        HUSKY_LOG(jsonlinestest::JsonTestLog::Loaded, "dir/\"a\".png", 3);
        HUSKY_LOG(jsonlinestest::JsonTestLog::Rate, 0.5, true); // Verbose も書く(既定は全部)
        HUSKY_LOG_TEXT(Info, "JsonTest.Text", "改行\nあり");
        sink.Flush();

        const std::vector<std::string> lines = writer.Lines();
        REQUIRE(lines.size() == 3);
        const std::string& first = lines[0];
        CHECK(first.front() == '{');
        CHECK(first.back() == '}');
        CHECK(Contains(first, "\"level\":\"Warning\""));
        CHECK(Contains(first, "\"channel\":\"JsonTest.Sink\""));
        CHECK(Contains(first, "\"id\":1,\"message\":\"Loaded\""));
        CHECK(Contains(first, "\"args\":{\"path\":\"dir/\\\"a\\\".png\",\"count\":3}")); // " は \" に書き換わる
        CHECK(Contains(first, "\"text\":\"dir/\\\"a\\\".png を読みました(3 個)\""));

        CHECK(Contains(lines[1], "\"args\":{\"rate\":0.5,\"ok\":true}"));
        // ID の無いログは id 0 と文だけ。改行は \n に書き換わり、1行のまま。
        CHECK(Contains(lines[2], "\"id\":0,\"text\":\"改行\\nあり\""));
        CHECK(lines[2].find('\n') == std::string::npos);
        CHECK(writer.flushCount >= 1); // 入れ物が空になったら書き出し先を Flush する
    }
}

TEST_CASE("JsonLinesLogSink は入れ物がいっぱいなら捨てて数え、dropped の行を書く")
{
    MemoryLineWriter writer;
    JsonLinesLogSink sink(writer, 2);
    // 書き出しのスレッドより速く積むので、2 件を超えた分は捨てられることがある(何件捨てるかはスレッドの速さで変わる)。
    {
        ScopedLogSink attach(sink);
        for (int i = 0; i < 200; ++i)
            HUSKY_LOG(jsonlinestest::JsonTestLog::Loaded, "x.png", i);
    }
    sink.Flush();

    const std::vector<std::string> lines = writer.Lines();
    const std::uint64_t dropped = sink.GetDroppedCount();
    std::uint64_t reported = 0;
    std::size_t recordLines = 0;
    for (const std::string& line : lines)
    {
        const std::size_t at = line.find("\"dropped\":");
        if (at != std::string::npos)
            reported += std::stoull(line.substr(at + 10));
        else
            ++recordLines;
    }
    // 書いた記録 + 捨てた数 = 出した数。捨てた数は全部 dropped の行に書かれている。
    CHECK(recordLines + dropped == 200);
    CHECK(reported == dropped);
}

TEST_CASE("JsonLinesLogSink のチャンネルごとの絞り込み")
{
    MemoryLineWriter writer;
    JsonLinesLogSink sink(writer);
    sink.GetFilter().SetLevel("JsonTest", LogLevel::Warning); // JsonTest の下は Warning 以上
    {
        ScopedLogSink attach(sink);
        HUSKY_LOG(jsonlinestest::JsonTestLog::Rate, 1.0, false); // Verbose: 書かない
        HUSKY_LOG(jsonlinestest::JsonTestLog::Loaded, "y.png", 1);
    }
    sink.Flush();
    const std::vector<std::string> lines = writer.Lines();
    REQUIRE(lines.size() == 1);
    CHECK(Contains(lines[0], "\"message\":\"Loaded\""));
}

TEST_CASE("FormatRecordJson は入れ物が小さければ、壊れた JSON を書かずに短い形にする")
{
    husky::core::LogRecord record;
    record.channel = static_cast<std::uint16_t>(Log::Get().RegisterChannel("JsonTest.Sink"));
    record.id = 1;
    record.level = LogLevel::Warning;
    record.AddArg(husky::core::LogArg::String(std::string(300, 'a').c_str()));
    record.AddArg(husky::core::LogArg::Int(1));

    char small[160];
    Log::Get().FormatRecordJson(record, small, sizeof(small));
    const std::string text = small;
    CHECK(text.front() == '{');
    CHECK(text.back() == '}');
    CHECK(Contains(text, "\"truncated\":true"));
}
