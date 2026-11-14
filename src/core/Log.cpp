// ログ(LOG-1)。仕組みは Log.h の最初のコメント。
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
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
        BadChannelName = 7,
    };

    HUSKY_LOG_CHANNEL_BEGIN(LogLog, "Engine.Log")
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
        HUSKY_LOG_MESSAGE(BadChannelName, Error, "チャンネルの名前 \"{name}\" は使えません(. で区切った1つ1つが、空でない英数字と _)",
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
            bool overflowed = false; // 入りきらずに捨てた分があるか

            void Append(const char* text, std::size_t count)
            {
                if (size == 0)
                    return;
                const std::size_t room = size - 1 - length;
                const std::size_t n = std::min(count, room);
                overflowed = overflowed || count > room;
                std::memcpy(buffer + length, text, n);
                length += n;
                buffer[length] = '\0';
            }
            void Append(const char* text) { Append(text, std::strlen(text)); }
        };

        void AppendArg(TextWriter& out, const LogRecord& record, int index)
        {
            const LogRecord::Arg& arg = record.args[index];
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
                out.Append(record.GetString(index));
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
        arg.text = value;
        return arg;
    }

    bool LogRecord::AddArg(const LogArg& arg)
    {
        if (argCount >= MaxLogArgs)
            return false;
        Arg& stored = args[argCount];
        stored = Arg{};
        stored.type = arg.type;
        stored.intValue = arg.intValue;
        stored.floatValue = arg.floatValue;
        stored.boolValue = arg.boolValue;
        if (arg.type == LogArgType::String)
        {
            // text の空いているところへ、終わりの 0 まで写す。入りきらなければ切る。
            stored.textOffset = textLength;
            const std::size_t room = MaxLogTextBytes - textLength; // 0 を含めて書ける大きさ(textLength は必ず MaxLogTextBytes 未満)
            const char* value = arg.text != nullptr ? arg.text : "";
            std::size_t length = std::strlen(value);
            if (length > room - 1)
            {
                // UTF-8 の1文字(日本語は 3 バイト)の途中で切らないように、文字の続きのバイト(10xxxxxx)の前まで戻る。
                length = room - 1;
                while (length > 0 && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80)
                    --length;
            }
            std::memcpy(text + textLength, value, length);
            text[textLength + length] = '\0';
            // 次の文字列は 0 の後ろから。いっぱいなら最後の 0 を共有する(後の文字列は空になる)。
            textLength = static_cast<std::uint16_t>(std::min<std::size_t>(textLength + length + 1, MaxLogTextBytes - 1));
        }
        ++argCount;
        return true;
    }

    const char* LogRecord::GetString(int index) const
    {
        if (index < 0 || index >= argCount || args[index].type != LogArgType::String)
            return "";
        return text + args[index].textOffset;
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

    int Log::FindChannelLocked(const char* name, std::size_t length) const
    {
        for (std::size_t i = 0; i < m_channels.size(); ++i)
        {
            const std::string& channel = m_channels[i].name;
            if (channel.size() == length && std::memcmp(channel.data(), name, length) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    int Log::FindChannel(const char* name)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return name != nullptr ? FindChannelLocked(name, std::strlen(name)) : -1;
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
        if (name == nullptr)
            name = "";
        // 名前を確かめる: . で区切った1つ1つが空でなく、英数字と _ だけ。
        bool valid = name[0] != '\0';
        std::size_t segmentLength = 0;
        for (const char* p = name; valid && *p != '\0'; ++p)
        {
            const char c = *p;
            if (c == '.')
            {
                valid = segmentLength > 0;
                segmentLength = 0;
            }
            else
            {
                valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                ++segmentLength;
            }
        }
        valid = valid && segmentLength > 0; // 最後が . で終わらない
        if (!valid)
        {
            HUSKY_LOG(LogLog::BadChannelName, name);
            return -1;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            const std::size_t length = std::strlen(name);
            const int found = FindChannelLocked(name, length);
            if (found >= 0)
                return found;

            // 上の段から順に、無ければ作る("App" → "App.Enemy" → "App.Enemy.System")。
            int parent = -1;
            bool full = false;
            for (std::size_t end = 0; end <= length && !full; ++end)
            {
                if (end != length && name[end] != '.')
                    continue;
                int index = FindChannelLocked(name, end);
                if (index < 0)
                {
                    if (static_cast<int>(m_channels.size()) >= MaxChannels)
                    {
                        full = true;
                        break;
                    }
                    ChannelInfo channel;
                    channel.name.assign(name, end);
                    channel.parent = parent;
                    m_channels.push_back(std::move(channel));
                    index = static_cast<int>(m_channels.size()) - 1;
                }
                parent = index;
            }
            if (!full)
                return parent; // いちばん下の段
        }
        // エラーは mutex を放してから出す(出す側も同じ mutex を取るため)。
        HUSKY_LOG(LogLog::TooManyChannels, name);
        return -1;
    }

    int Log::GetParentChannel(int channel)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (channel < 0 || channel >= static_cast<int>(m_channels.size()))
            return -1;
        return m_channels[channel].parent;
    }

    bool Log::IsUnder(int channel, int ancestor)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (ancestor < 0)
            return false;
        // 親をたどって ancestor に着けば、その下にある。
        while (channel >= 0 && channel < static_cast<int>(m_channels.size()))
        {
            if (channel == ancestor)
                return true;
            channel = m_channels[channel].parent;
        }
        return false;
    }

    LogChannelFilter::LogChannelFilter(LogLevel defaultMinimum)
        : m_default(defaultMinimum)
    {
        for (std::int8_t& level : m_levels)
            level = Unset;
    }

    bool LogChannelFilter::SetLevel(const char* channelPath, LogLevel minimum)
    {
        const int channel = Log::Get().RegisterChannel(channelPath);
        if (channel < 0)
            return false;
        m_levels[channel] = static_cast<std::int8_t>(minimum);
        return true;
    }

    void LogChannelFilter::ClearLevel(const char* channelPath)
    {
        const int channel = Log::Get().FindChannel(channelPath);
        if (channel >= 0)
            m_levels[channel] = Unset;
    }

    bool LogChannelFilter::Passes(const LogRecord& record) const
    {
        // 自分から親へたどり、最初に見つかった決めごとを使う(子の決めごとが親より優先)。
        Log& log = Log::Get();
        int channel = record.channel;
        while (channel >= 0 && channel < Log::MaxChannels)
        {
            if (m_levels[channel] != Unset)
                return static_cast<int>(record.level) >= m_levels[channel];
            channel = log.GetParentChannel(channel);
        }
        return record.level >= m_default;
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
                else if (message.id < info.messages.size() && info.messages[message.id].registered &&
                         info.messages[message.id].name != messageName)
                {
                    // 同じ番号に別の名前のメッセージ: 番号の振り間違いなので断る
                    error = LogLog::DuplicateMessage;
                }
                else
                {
                    // 初めての番号か、同じ番号・同じ名前の登録し直し(HR-2)。登録し直しは、後の定義で上書きする。
                    // 同じヘッダのメッセージを exe と DLL の両方が登録する(ヘッダの inline の static がモジュールごとに
                    // あるため)ことと、ゲームの DLL を読み直すと、直した文で登録し直されることがあるため。
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
                        Deliver(channel, message.level, id, args, argCount);
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

    void Log::WriteChecked(int channel, LogLevel level, std::uint16_t id, const LogArg* args, int argCount)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        Deliver(channel, level, id, args, argCount);
    }

    void Log::WriteText(int channel, LogLevel level, const char* text)
    {
        const LogArg arg = LogArg::String(text);
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        Deliver(channel, level, LogTextId, &arg, 1);
    }

    void Log::Deliver(int channel, LogLevel level, std::uint16_t id, const LogArg* args, int argCount)
    {
        if (m_sinks.empty())
            return; // 受け手がいなければ、記録を作る手間も省く
        // 記録は 1 件ずつ使い回す(mutex を持っている間だけ使うので、1つで足りる。スタックに大きな構造体を置かずに済む)。
        LogRecord& record = m_scratch;
        record = LogRecord{};
        record.channel = static_cast<std::uint16_t>(channel < 0 ? 0xFFFF : channel);
        record.level = level;
        record.id = id;
        for (int i = 0; i < argCount; ++i)
            record.AddArg(args[i]);
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

        if (record.id == LogTextId)
        {
            out.Append(record.GetString(0)); // ID の無いログ: 文そのもの
            return out.length;
        }

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
                AppendArg(out, record, i);
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
                    AppendArg(out, record, static_cast<int>(i));
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

    namespace
    {
        // JSON の文字列として書く(前後の " と、" \ 制御文字の書き換え)。日本語などの UTF-8 はそのまま書く。
        void AppendJsonString(TextWriter& out, const char* text)
        {
            out.Append("\"");
            for (const char* p = text; *p != '\0'; ++p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c == '"')
                    out.Append("\\\"");
                else if (c == '\\')
                    out.Append("\\\\");
                else if (c == '\n')
                    out.Append("\\n");
                else if (c == '\r')
                    out.Append("\\r");
                else if (c == '\t')
                    out.Append("\\t");
                else if (c < 0x20)
                {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out.Append(escaped);
                }
                else
                    out.Append(p, 1);
            }
            out.Append("\"");
        }

        void AppendJsonArg(TextWriter& out, const LogRecord& record, int index)
        {
            if (record.args[index].type == LogArgType::String)
                AppendJsonString(out, record.GetString(index));
            else if (record.args[index].type == LogArgType::None ||
                     (record.args[index].type == LogArgType::Float && !std::isfinite(record.args[index].floatValue)))
                out.Append("null"); // JSON には NaN や無限大が無い
            else
                AppendArg(out, record, index); // 数と true / false は、そのまま JSON の値になる
        }
    }

    std::size_t Log::FormatRecordJson(const LogRecord& record, char* buffer, std::size_t size)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        std::size_t length = 0;
        // 入りきらなければ、文を省いてもう一度。それでも入らなければ、値も省いた短い形にする(壊れた JSON は書かない)。
        if (FormatJsonLocked(record, buffer, size, true, length) || FormatJsonLocked(record, buffer, size, false, length))
            return length;
        TextWriter out{buffer, size};
        char head[160];
        std::snprintf(head, sizeof(head), "{\"time\":%.6f,\"level\":\"%s\",\"id\":%u,\"truncated\":true}",
                      record.timeSeconds, ToString(record.level), static_cast<unsigned>(record.id));
        out.Append(head);
        return out.length;
    }

    bool Log::FormatJsonLocked(const LogRecord& record, char* buffer, std::size_t size, bool withText,
                               std::size_t& length) const
    {
        TextWriter out{buffer, size};
        if (size > 0)
            buffer[0] = '\0';

        char head[160];
        std::snprintf(head, sizeof(head), "{\"time\":%.6f,\"frame\":%llu,\"thread\":%u,\"level\":\"%s\",\"channel\":",
                      record.timeSeconds, static_cast<unsigned long long>(record.frame),
                      static_cast<unsigned>(record.thread), ToString(record.level));
        out.Append(head);
        const ChannelInfo* channel = record.channel < m_channels.size() ? &m_channels[record.channel] : nullptr;
        AppendJsonString(out, channel != nullptr ? channel->name.c_str() : "?");
        char id[32];
        std::snprintf(id, sizeof(id), ",\"id\":%u", static_cast<unsigned>(record.id));
        out.Append(id);

        if (record.id != LogTextId)
        {
            const MessageInfo* message = nullptr;
            if (channel != nullptr && record.id < channel->messages.size() && channel->messages[record.id].registered)
                message = &channel->messages[record.id];
            if (message != nullptr)
            {
                // 値は名前つき("args":{"path":"a.png"})。ビュワーが名前で絞り込めるように。
                out.Append(",\"message\":");
                AppendJsonString(out, message->name.c_str());
                out.Append(",\"args\":{");
                for (int i = 0; i < record.argCount && i < static_cast<int>(message->args.size()); ++i)
                {
                    if (i > 0)
                        out.Append(",");
                    AppendJsonString(out, message->args[i].name.c_str());
                    out.Append(":");
                    AppendJsonArg(out, record, i);
                }
                out.Append("}");
            }
            else
            {
                // 定義が無い: 値を並びのまま書く。
                out.Append(",\"args\":[");
                for (int i = 0; i < record.argCount; ++i)
                {
                    if (i > 0)
                        out.Append(",");
                    AppendJsonArg(out, record, i);
                }
                out.Append("]");
            }
        }

        if (withText || record.id == LogTextId)
        {
            char text[1024];
            const std::size_t textLength = FormatLocked(record, text, sizeof(text));
            // FormatLocked は "[レベル][チャンネル] " を前に付けるので、その後ろだけを書く(最初の "] " はチャンネルの後ろ)。
            const char* body = std::strstr(text, "] ");
            out.Append(",\"text\":");
            AppendJsonString(out, body != nullptr ? body + 2 : (textLength > 0 ? text : ""));
        }
        out.Append("}");
        length = out.length;
        return !out.overflowed;
    }

    int Log::GetChannelCount()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return static_cast<int>(m_channels.size());
    }

    std::vector<Log::MessageSnapshot> Log::GetMessages(int channel)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        std::vector<MessageSnapshot> result;
        if (channel < 0 || channel >= static_cast<int>(m_channels.size()))
            return result;
        const ChannelInfo& info = m_channels[channel];
        for (std::size_t id = 0; id < info.messages.size(); ++id)
        {
            const MessageInfo& message = info.messages[id];
            if (!message.registered)
                continue;
            MessageSnapshot snapshot;
            snapshot.id = static_cast<std::uint16_t>(id);
            snapshot.name = message.name;
            snapshot.level = message.level;
            snapshot.format = message.format;
            for (const ArgInfo& arg : message.args)
                snapshot.args.push_back({arg.name, arg.type});
            result.push_back(std::move(snapshot));
        }
        return result;
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
        if (!m_filter.Passes(record))
            return;
        char line[1024];
        Log::Get().FormatRecord(record, line, sizeof(line));
        std::FILE* out = m_stream == Stream::StandardError ? stderr : stdout;
        std::fprintf(out, "%s\n", line);
        std::fflush(out); // 溜めずにすぐ書き出す(ファイルへ流しているときも、落ちる直前のログが残るように)
    }

    void WriteLogTextFormat(int channel, LogLevel level, const char* format, ...)
    {
        char text[MaxLogTextBytes];
        va_list args; // 可変個の引数(...)を順に読むための道具
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        Log::Get().WriteText(channel, level, text);
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
