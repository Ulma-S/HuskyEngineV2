// ログ(core/Log.h)のテスト。受け手(LogSink)で記録を受け取り、中身と、文にした結果を確かめる。
// 値の数や種類の違いはコンパイルエラーになる(static_assert)ので、ここでは確かめられない。
#include "core/Log.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

using husky::core::Log;
using husky::core::LogArg;
using husky::core::LogArgDef;
using husky::core::LogArgType;
using husky::core::LogLevel;
using husky::core::LogMessageDef;
using husky::core::LogRecord;
using husky::core::LogSink;
using husky::core::ScopedLogSink;

namespace testlog
{
    // テスト用のチャンネル(番号は手で振る)。
    enum class TestLog : std::uint16_t
    {
        Loaded = 1,
        NoArgs = 2,
        Mixed = 5, // すき間があってよい
    };

    HUSKY_LOG_CHANNEL_BEGIN(TestLog, "Test")
        HUSKY_LOG_MESSAGE(Loaded, Warning, "{path} を読みました({count} 個)", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(count, Int))
        HUSKY_LOG_MESSAGE(NoArgs, Info, "値の無いメッセージ")
        HUSKY_LOG_MESSAGE(Mixed, Error, "{rate} {ok} {missing}", HUSKY_LOG_ARG(rate, Float), HUSKY_LOG_ARG(ok, Bool))
    HUSKY_LOG_CHANNEL_END()
}

namespace
{
    // 受け取った記録を貯めるだけの受け手。
    class CaptureSink : public LogSink
    {
    public:
        void Receive(const LogRecord& record) override { records.push_back(record); }
        std::vector<LogRecord> records;
    };

    std::string Format(const LogRecord& record)
    {
        char buffer[512];
        Log::Get().FormatRecord(record, buffer, sizeof(buffer));
        return buffer;
    }
}

TEST_CASE("HUSKY_LOG はチャンネル・番号・レベル・値を記録に入れ、受け手が文にする")
{
    CaptureSink sink;
    ScopedLogSink attach(sink);

    const std::string path = "textures/enemy.png";
    HUSKY_LOG(testlog::TestLog::Loaded, path, 3);
    HUSKY_LOG(testlog::TestLog::NoArgs);
    HUSKY_LOG(testlog::TestLog::Mixed, 0.5f, true);

    REQUIRE(sink.records.size() == 3);
    const LogRecord& first = sink.records[0];
    CHECK(std::strcmp(Log::Get().GetChannelName(first.channel), "Test") == 0);
    CHECK(first.id == 1);
    CHECK(first.level == LogLevel::Warning);
    CHECK(first.argCount == 2);
    CHECK(first.args[0].type == LogArgType::String);
    CHECK(first.args[1].intValue == 3);
    CHECK(first.thread != 0);
    CHECK(Format(first) == "[Warning][Test] textures/enemy.png を読みました(3 個)");

    CHECK(Format(sink.records[1]) == "[Info][Test] 値の無いメッセージ");
    // 定義に無い名前の {missing} は、そのまま残る。
    CHECK(Format(sink.records[2]) == "[Error][Test] 0.5 true {missing}");
}

TEST_CASE("フレームの番号は AdvanceFrame で進む")
{
    CaptureSink sink;
    ScopedLogSink attach(sink);

    HUSKY_LOG(testlog::TestLog::NoArgs);
    Log::Get().AdvanceFrame();
    HUSKY_LOG(testlog::TestLog::NoArgs);

    REQUIRE(sink.records.size() == 2);
    CHECK(sink.records[1].frame == sink.records[0].frame + 1);
}

TEST_CASE("同じ名前のチャンネルは同じ番号になり、テンプレートを使わない窓口(スクリプト用)からも足して出せる")
{
    Log& log = Log::Get();
    const int channel = log.RegisterChannel("Test");
    CHECK(channel == log.FindChannel("Test"));

    // スクリプトが同じチャンネルに番号 10 を足す、という想定。
    LogMessageDef message;
    message.id = 10;
    message.name = "FromScript";
    message.level = LogLevel::Info;
    message.format = "スクリプトから {value}";
    message.args[0] = LogArgDef{"value", LogArgType::Int};
    CHECK(log.RegisterMessage(channel, message));

    CaptureSink sink;
    ScopedLogSink attach(sink);
    const LogArg args[] = {LogArg::Int(42)};
    CHECK(log.Write(channel, 10, args, 1));
    REQUIRE(sink.records.size() == 1);
    CHECK(Format(sink.records[0]) == "[Info][Test] スクリプトから 42");
}

TEST_CASE("番号が重なる・番号 0 の登録と、値の違う Write は断り、Log チャンネルのエラーを出す")
{
    Log& log = Log::Get();
    const int channel = log.RegisterChannel("Test");
    const int logChannel = log.FindChannel("Log");
    REQUIRE(logChannel >= 0);

    CaptureSink sink;
    ScopedLogSink attach(sink);

    LogMessageDef duplicate;
    duplicate.id = 1; // TestLog::Loaded と同じ番号
    duplicate.name = "Other";
    CHECK_FALSE(log.RegisterMessage(channel, duplicate));

    LogMessageDef zero;
    zero.id = 0;
    zero.name = "Zero";
    CHECK_FALSE(log.RegisterMessage(channel, zero));

    const LogArg wrongType[] = {LogArg::Int(1), LogArg::Int(2)}; // Loaded の1つ目は String
    CHECK_FALSE(log.Write(channel, 1, wrongType, 2));
    CHECK_FALSE(log.Write(channel, 1, wrongType, 1)); // 数が違う
    CHECK_FALSE(log.Write(channel, 99, nullptr, 0));  // 無い番号

    REQUIRE(sink.records.size() == 5);
    for (const LogRecord& record : sink.records)
    {
        CHECK(record.channel == logChannel);
        CHECK(record.level == LogLevel::Error);
    }
    CHECK(Format(sink.records[0]) == "[Error][Log] Test の番号 1 はもう登録されています(Other)");
    CHECK(Format(sink.records[4]) == "[Error][Log] Test に番号 99 のメッセージがありません");

    // 断った後も、元の定義はそのまま。
    sink.records.clear();
    HUSKY_LOG(testlog::TestLog::Loaded, "a.png", 1);
    REQUIRE(sink.records.size() == 1);
    CHECK(Format(sink.records[0]) == "[Warning][Test] a.png を読みました(1 個)");
}

TEST_CASE("文字列の値は記録の中の入れ物に写り、入りきらなければ UTF-8 の文字の途中で切らずに切る")
{
    LogRecord record;
    CHECK(record.AddArg(LogArg::String("abc")));
    std::string text;
    for (int i = 0; i < 200; ++i)
        text += "あ"; // 3 バイト × 200 = 600 バイト(入れ物の残りは 512 - 4 = 508 バイト、0 を除いて 507)
    CHECK(record.AddArg(LogArg::String(text.c_str())));
    CHECK(record.AddArg(LogArg::String("後ろ"))); // もう入らないので空になる

    CHECK(std::string(record.GetString(0)) == "abc");
    const std::size_t length = std::strlen(record.GetString(1));
    CHECK(length % 3 == 0); // 「あ」の切れ端が残らない
    CHECK(length == 507 / 3 * 3);
    CHECK(std::string(record.GetString(2)).empty());

    // 記録をコピーしても、文字列は場所(先頭からのバイト数)で指しているので、写した方の中身を指す。
    const LogRecord copy = record;
    CHECK(copy.GetString(0) == copy.text + 0);
    CHECK(std::string(copy.GetString(0)) == "abc");
}

TEST_CASE("HUSKY_LOG_TEXT は ID の無いログ(番号 0)を、名前のチャンネルで出す")
{
    CaptureSink sink;
    ScopedLogSink attach(sink);

    HUSKY_LOG_TEXT(Info, "TextTest", "x = %d, name = %s", 7, "player");
    REQUIRE(sink.records.size() == 1);
    CHECK(sink.records[0].id == husky::core::LogTextId);
    CHECK(sink.records[0].channel == Log::Get().FindChannel("TextTest"));
    CHECK(Format(sink.records[0]) == "[Info][TextTest] x = 7, name = player");
}

TEST_CASE("FormatRecord は入れ物が小さいと切り、必ず 0 で終える")
{
    CaptureSink sink;
    ScopedLogSink attach(sink);
    HUSKY_LOG(testlog::TestLog::NoArgs);
    REQUIRE(sink.records.size() == 1);

    char small[8];
    const std::size_t written = Log::Get().FormatRecord(sink.records[0], small, sizeof(small));
    CHECK(written == 7);
    CHECK(std::string(small) == "[Info][");
}

TEST_CASE("HUSKY_ASSERT は外れたときだけ記録する(製品ではコードごと抜けて、条件も計算しない)")
{
    Log& log = Log::Get();
    const bool previous = log.GetBreakOnAssert();
    log.SetBreakOnAssert(false); // テストではデバッガで止めない

    CaptureSink sink;
    ScopedLogSink attach(sink);

    int evaluated = 0;
    HUSKY_ASSERT(++evaluated > 0);  // 成り立つ(本当のコードでは、条件に副作用を書かないこと)
    HUSKY_ASSERT(++evaluated < 0);  // 外れる

#if HUSKY_DEVELOPMENT
    CHECK(evaluated == 2);
    REQUIRE(sink.records.size() == 1);
    CHECK(sink.records[0].level == LogLevel::Assert);
    CHECK(Format(sink.records[0]).find("[Assert][Assert] ++evaluated < 0 が成り立ちません(LogTests.cpp:") == 0);
#else
    CHECK(evaluated == 0);
    CHECK(sink.records.empty());
#endif

    log.SetBreakOnAssert(previous);
}
