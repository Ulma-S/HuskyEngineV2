// ログ(LOG-1)。仕組みは Log.h の最初のコメント。
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h> // __debugbreak(デバッガで止める。コンパイラの組み込みの関数で、OS の API ではない)
#endif

namespace husky::core
{
    // ログの仕組み自身が出すメッセージ(使い方の誤り)。
    enum class LogLog : std::uint16_t
    {
        BadArguments = 1,
        UnknownMessage = 2,
        DuplicateMessage = 3,
        BadMessage = 4,
        UnknownChannel = 5,
        TooManyChannels = 6,
    };

    HUSKY_LOG_CHANNEL_BEGIN(LogLog, "Log")
        HUSKY_LOG_MESSAGE(BadArguments, Error, "{channel} の {message} に渡した値の数か種類が、定義と違います",
                          HUSKY_LOG_ARG(channel, String), HUSKY_LOG_ARG(message, String))
        HUSKY_LOG_MESSAGE(UnknownMessage, Error, "{channel} に番号 {id} のメッセージがありません",
                          HUSKY_LOG_ARG(channel, String), HUSKY_LOG_ARG(id, Int))
        HUSKY_LOG_MESSAGE(DuplicateMessage, Error, "{channel} の番号 {id} はもう登録されています({name})",
                          HUSKY_LOG_ARG(channel, String), HUSKY_LOG_ARG(id, Int), HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(BadMessage, Error, "{channel} の {name} は登録できません(番号が 0 か、値の名前が無い)",
                          HUSKY_LOG_ARG(channel, String), HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(UnknownChannel, Error, "番号 {channel} のチャンネルはありません", HUSKY_LOG_ARG(channel, Int))
        HUSKY_LOG_MESSAGE(TooManyChannels, Error, "チャンネルが多すぎて {name} を登録できません",
                          HUSKY_LOG_ARG(name, String))
    HUSKY_LOG_CHANNEL_END()

    // HUSKY_ASSERT が外れたとき。
    enum class AssertLog : std::uint16_t
    {
        Failed = 1,
    };

    HUSKY_LOG_CHANNEL_BEGIN(AssertLog, "Assert")
        HUSKY_LOG_MESSAGE(Failed, Assert, "{condition} が成り立ちません({file}:{line})",
                          HUSKY_LOG_ARG(condition, String), HUSKY_LOG_ARG(file, String), HUSKY_LOG_ARG(line, Int))
    HUSKY_LOG_CHANNEL_END()

    namespace
    {
        std::int64_t NowNanoseconds()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        // スレッドの番号(そのスレッドが最初にログを出したときに 1 から振る)。
        std::uint32_t CurrentThreadNumber()
        {
            static std::atomic<std::uint32_t> next{1};
            thread_local std::uint32_t number = 0; // thread_local: スレッドごとに別の変数になる
            if (number == 0)
                number = next.fetch_add(1, std::memory_order_relaxed);
            return number;
        }

        // 入れ物に文字を足していく小さな道具(確保しない。あふれた分は捨てる)。
        struct TextWriter
        {
            char* buffer;
            std::size_t size;
            std::size_t length = 0;

            void Append(const char* text, std::size_t count)
            {
                if (size == 0)
                    return;
                const std::size_t room = size - 1 - length;
                const std::size_t n = std::min(count, room);
                std::memcpy(buffer + length, text, n);
                length += n;
                buffer[length] = '\0';
            }
            void Append(const char* text) { Append(text, std::strlen(text)); }
        };

        void AppendArg(TextWriter& out, const LogArg& arg)
        {
            char number[64];
            switch (arg.type)
            {
            case LogArgType::Int:
                std::snprintf(number, sizeof(number), "%lld", static_cast<long long>(arg.intValue));
                out.Append(number);
                break;
            case LogArgType::Float:
                std::snprintf(number, sizeof(number), "%g", arg.floatValue);
                out.Append(number);
                break;
            case LogArgType::Bool:
                out.Append(arg.boolValue ? "true" : "false");
                break;
            case LogArgType::String:
                out.Append(arg.text);
                break;
            case LogArgType::None:
                break;
            }
        }
    }

    const char* ToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Verbose: return "Verbose";
        case LogLevel::Info: return "Info";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Error: return "Error";
        case LogLevel::Assert: return "Assert";
        }
        return "?";
    }

    const char* ToString(LogArgType type)
    {
        switch (type)
        {
        case LogArgType::None: return "None";
        case LogArgType::Int: return "Int";
        case LogArgType::Float: return "Float";
        case LogArgType::Bool: return "Bool";
        case LogArgType::String: return "String";
        }
        return "?";
    }

    LogArg LogArg::Int(std::int64_t value)
    {
        LogArg arg;
        arg.type = LogArgType::Int;
        arg.intValue = value;
        return arg;
    }

    LogArg LogArg::Float(double value)
    {
        LogArg arg;
        arg.type = LogArgType::Float;
        arg.floatValue = value;
        return arg;
    }

    LogArg LogArg::Bool(bool value)
    {
        LogArg arg;
        arg.type = LogArgType::Bool;
        arg.boolValue = value;
        return arg;
    }

    LogArg LogArg::String(const char* value)
    {
        LogArg arg;
        arg.type = LogArgType::String;
        if (value == nullptr)
            return arg;
        std::size_t length = std::strlen(value);
        if (length > MaxLogArgTextLength - 1)
        {
            // 長ければ切る。UTF-8 の1文字(日本語は 3 バイト)の途中で切らないように、文字の続きのバイト(10xxxxxx)の前まで戻る。
            length = MaxLogArgTextLength - 1;
            while (length > 0 && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80)
                --length;
        }
        std::memcpy(arg.text, value, length);
        arg.text[length] = '\0';
        return arg;
    }

    Log& Log::Get()
    {
        // 関数の中の static: 最初に呼んだときに1回だけ作る(C++11 から、複数のスレッドから同時に呼んでも1回だけ)。
        // プログラムの始まりの、ほかの static の初期化(チャンネルの登録)から呼ばれても、作られる前に使われることがない。
        static Log log;
        return log;
    }

    Log::Log()
        : m_startNanoseconds(NowNanoseconds())
    {
    }

    int Log::FindChannelLocked(const char* name) const
    {
        for (std::size_t i = 0; i < m_channels.size(); ++i)
        {
            if (m_channels[i].name == name)
                return static_cast<int>(i);
        }
        return -1;
    }

    int Log::FindChannel(const char* name)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return FindChannelLocked(name);
    }

    const char* Log::GetChannelName(int channel)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (channel < 0 || channel >= static_cast<int>(m_channels.size()))
            return nullptr;
        return m_channels[channel].name.c_str();
    }

    int Log::RegisterChannel(const char* name)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            const int found = FindChannelLocked(name);
            if (found >= 0)
                return found;
            if (static_cast<int>(m_channels.size()) < MaxChannels)
            {
                ChannelInfo channel;
                channel.name = name;
                m_channels.push_back(std::move(channel));
                return static_cast<int>(m_channels.size()) - 1;
            }
        }
        // エラーは mutex を放してから出す(出す側も同じ mutex を取るため)。
        HUSKY_LOG(LogLog::TooManyChannels, name);
        return -1;
    }

    bool Log::RegisterMessage(int channel, const LogMessageDef& message)
    {
        const char* messageName = message.name != nullptr ? message.name : "";
        LogLog error = LogLog::BadMessage;
        std::string channelName;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (channel < 0 || channel >= static_cast<int>(m_channels.size()))
            {
                error = LogLog::UnknownChannel;
            }
            else
            {
                ChannelInfo& info = m_channels[channel];
                channelName = info.name;
                bool namesOk = true;
                for (int i = 0; i < message.ArgCount(); ++i)
                    namesOk = namesOk && message.args[i].name != nullptr;

                if (message.id == 0 || !namesOk)
                {
                    error = LogLog::BadMessage;
                }
                else if (message.id < info.messages.size() && info.messages[message.id].registered)
                {
                    error = LogLog::DuplicateMessage;
                }
                else
                {
                    if (message.id >= info.messages.size())
                        info.messages.resize(static_cast<std::size_t>(message.id) + 1);
                    MessageInfo& entry = info.messages[message.id];
                    entry.registered = true;
                    entry.name = messageName;
                    entry.level = message.level;
                    entry.format = message.format != nullptr ? message.format : "";
                    entry.args.clear();
                    for (int i = 0; i < message.ArgCount(); ++i)
                        entry.args.push_back({message.args[i].name, message.args[i].type});
                    return true;
                }
            }
        }
        switch (error)
        {
        case LogLog::UnknownChannel: HUSKY_LOG(LogLog::UnknownChannel, channel); break;
        case LogLog::DuplicateMessage:
            HUSKY_LOG(LogLog::DuplicateMessage, channelName, static_cast<int>(message.id), messageName);
            break;
        default: HUSKY_LOG(LogLog::BadMessage, channelName, messageName); break;
        }
        return false;
    }

    bool Log::Write(int channel, std::uint16_t id, const LogArg* args, int argCount)
    {
        LogLog error = LogLog::BadArguments;
        std::string channelName;
        std::string messageName;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (channel < 0 || channel >= static_cast<int>(m_channels.size()))
            {
                error = LogLog::UnknownChannel;
            }
            else
            {
                const ChannelInfo& info = m_channels[channel];
                channelName = info.name;
                if (id >= info.messages.size() || !info.messages[id].registered)
                {
                    error = LogLog::UnknownMessage;
                }
                else
                {
                    const MessageInfo& message = info.messages[id];
                    messageName = message.name;
                    bool ok = argCount == static_cast<int>(message.args.size()) && argCount <= MaxLogArgs;
                    for (int i = 0; ok && i < argCount; ++i)
                        ok = args[i].type == message.args[i].type;
                    if (ok)
                    {
                        LogRecord record;
                        record.level = message.level;
                        record.channel = static_cast<std::uint16_t>(channel);
                        record.id = id;
                        record.argCount = static_cast<std::uint8_t>(argCount);
                        for (int i = 0; i < argCount; ++i)
                            record.args[i] = args[i];
                        Deliver(record);
                        return true;
                    }
                }
            }
        }
        switch (error)
        {
        case LogLog::UnknownChannel: HUSKY_LOG(LogLog::UnknownChannel, channel); break;
        case LogLog::UnknownMessage: HUSKY_LOG(LogLog::UnknownMessage, channelName, static_cast<int>(id)); break;
        default: HUSKY_LOG(LogLog::BadArguments, channelName, messageName); break;
        }
        return false;
    }

    void Log::WriteChecked(int channel, LogRecord& record)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        record.channel = static_cast<std::uint16_t>(channel);
        Deliver(record);
    }

    void Log::Deliver(LogRecord& record)
    {
        record.timeSeconds = static_cast<double>(NowNanoseconds() - m_startNanoseconds) / 1e9;
        record.frame = m_frame.load(std::memory_order_relaxed);
        record.thread = CurrentThreadNumber();
        for (LogSink* sink : m_sinks)
            sink->Receive(record);
    }

    void Log::AddSink(LogSink* sink)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (std::find(m_sinks.begin(), m_sinks.end(), sink) == m_sinks.end())
            m_sinks.push_back(sink);
    }

    void Log::RemoveSink(LogSink* sink)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), sink), m_sinks.end());
    }

    std::size_t Log::FormatRecord(const LogRecord& record, char* buffer, std::size_t size)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return FormatLocked(record, buffer, size);
    }

    std::size_t Log::FormatLocked(const LogRecord& record, char* buffer, std::size_t size) const
    {
        TextWriter out{buffer, size};
        if (size > 0)
            buffer[0] = '\0';

        const ChannelInfo* channel = record.channel < m_channels.size() ? &m_channels[record.channel] : nullptr;
        out.Append("[");
        out.Append(ToString(record.level));
        out.Append("][");
        out.Append(channel != nullptr ? channel->name.c_str() : "?");
        out.Append("] ");

        const MessageInfo* message = nullptr;
        if (channel != nullptr && record.id < channel->messages.size() && channel->messages[record.id].registered)
            message = &channel->messages[record.id];
        if (message == nullptr)
        {
            // 定義が無い(将来、別の版のカタログなど): 番号と値をそのまま並べる。
            char number[32];
            std::snprintf(number, sizeof(number), "#%u", static_cast<unsigned>(record.id));
            out.Append(number);
            for (int i = 0; i < record.argCount; ++i)
            {
                out.Append(" ");
                AppendArg(out, record.args[i]);
            }
            return out.length;
        }

        // 型紙の {名前} を、同じ名前の値に置き換える。知らない名前は、そのまま残す。
        const char* p = message->format.c_str();
        while (*p != '\0')
        {
            const char* open = std::strchr(p, '{');
            if (open == nullptr)
            {
                out.Append(p);
                break;
            }
            const char* close = std::strchr(open, '}');
            if (close == nullptr)
            {
                out.Append(p);
                break;
            }
            out.Append(p, static_cast<std::size_t>(open - p));
            const std::size_t nameLength = static_cast<std::size_t>(close - open - 1);
            bool replaced = false;
            for (std::size_t i = 0; i < message->args.size() && i < record.argCount; ++i)
            {
                const std::string& name = message->args[i].name;
                if (name.size() == nameLength && std::memcmp(name.data(), open + 1, nameLength) == 0)
                {
                    AppendArg(out, record.args[i]);
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                out.Append(open, static_cast<std::size_t>(close - open + 1));
            p = close + 1;
        }
        return out.length;
    }

    int RegisterLogChannelTable(const char* channelName, const LogMessageDef* messages, std::size_t count)
    {
        Log& log = Log::Get();
        const int channel = log.RegisterChannel(channelName);
        if (channel < 0)
            return -1;
        for (std::size_t i = 0; i < count; ++i)
            log.RegisterMessage(channel, messages[i]);
        return channel;
    }

    void ConsoleLogSink::Receive(const LogRecord& record)
    {
        char line[1024];
        Log::Get().FormatRecord(record, line, sizeof(line));
        std::printf("%s\n", line);
    }

    void ReportAssertFailure(const char* condition, const char* file, int line)
    {
        // ファイルの名前だけにする(フォルダまで書くと長く、値の入れ物に入りきらない)。
        const char* name = file;
        for (const char* p = file; *p != '\0'; ++p)
        {
            if (*p == '/' || *p == '\\')
                name = p + 1;
        }
        HUSKY_LOG(AssertLog::Failed, condition, name, line);

        if (Log::Get().GetBreakOnAssert())
        {
#if defined(_MSC_VER)
            __debugbreak();
#elif defined(__clang__)
            __builtin_debugtrap();
#else
            std::abort();
#endif
        }
    }
}
