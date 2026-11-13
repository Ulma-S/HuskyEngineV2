// ログ(LOG-1)。1件のログは文字列ではなく「どのメッセージか(チャンネル + 番号)と値」の記録(LogRecord)。
// 文(".meta がありません(foo.png)")を組み立てるのは受け手(LogSink)で、出す側は文字列の結合もメモリの確保もしない。
// 将来はテレメトリとして、別プロセスのビュワーが記録を絞り込み、カタログ(全部の定義)で文にする(設計メモ「ログ」)。
//
// メッセージの定義(モジュールごとのヘッダーに書く。チャンネルの enum と同じ名前空間に書く: ADL で見つけるため):
//   enum class AssetLog : std::uint16_t
//   {
//       MetaMissing = 1, // 番号は必ず手で書く。一度振ったら変えず、使わなくなっても使い回さない(過去のログと合わなくなる)
//       OrphanMeta = 2,
//   };
//   HUSKY_LOG_CHANNEL_BEGIN(AssetLog, "Engine.Asset")   // チャンネルの名前(正体はこの文字列。スクリプトも同じ名前で同じチャンネル)。
//                                                       // . でつなぐと入れ子("Engine" の下の "Asset")。絞り込みは親から子へ受け継ぐ
//       HUSKY_LOG_MESSAGE(MetaMissing, Warning, ".meta がありません({path})", HUSKY_LOG_ARG(path, String))
//       HUSKY_LOG_MESSAGE(OrphanMeta, Warning, "対になるファイルが無い .meta があります({path})", HUSKY_LOG_ARG(path, String))
//   HUSKY_LOG_CHANNEL_END()
//
// 出す側:
//   HUSKY_LOG(AssetLog::MetaMissing, path);   // 値の数と種類は、コンパイルのときに定義と照らし合わせる(違えばコンパイルエラー)
//
// 仕組み:
//   - 土台はテンプレートを使わない窓口(Log の RegisterChannel / RegisterMessage / Write)。スクリプト言語もこれを呼ぶ。
//     C++ のマクロは、定義の表(constexpr の配列)を作り、最初に使うときに窓口へ登録する薄い包み。
//   - HUSKY_LOG はテンプレート(template <auto Message>)で、どのメッセージかがコンパイルのときに分かるので、表を引いて
//     値の数・種類を static_assert で確かめ、レベルも記録に入れる。実行中にするのは、決まった大きさの記録に値を写すことだけ。
//   - 文の型紙の {名前} は、同じ名前の値に置き換わる(受け手が FormatRecord で組み立てる)。
//
// 動作スレッド: どのスレッドからも出せる(中は mutex 1つ)。受け手はその mutex を持ったまま呼ばれるので、受け手の中でログを出さない。
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace husky::core
{
    // 重要度。Assert は HUSKY_ASSERT が外れたとき(開発ビルドだけ)。
    enum class LogLevel : std::uint8_t
    {
        Verbose,
        Info,
        Warning,
        Error,
        Assert,
    };

    const char* ToString(LogLevel level);

    // 値の種類。int32 などの整数はすべて Int(int64 で持つ)、float は Float(double で持つ)。
    enum class LogArgType : std::uint8_t
    {
        None,
        Int,
        Float,
        Bool,
        String, // 文字列(記録の中の共有の入れ物に写す。1件の文字列の合計が MaxLogTextBytes - 1 バイトを超えると切る)
    };

    const char* ToString(LogArgType type);

    constexpr int MaxLogArgs = 8;          // 1件に持てる値の数
    constexpr int MaxLogTextBytes = 512;   // 1件の記録の、文字列の値をまとめて入れる入れ物の大きさ(それぞれの終わりの 0 を含む)
    constexpr std::uint16_t LogTextId = 0; // ID の無いログ(HUSKY_LOG_TEXT)の番号。値は文字列1つ(文そのもの)

    // 出す側が渡す1つの値。文字列は借りるだけ(Write の間だけ生きていればよい。記録へは写す)。
    struct LogArg
    {
        LogArgType type = LogArgType::None;
        std::int64_t intValue = 0;
        double floatValue = 0.0;
        bool boolValue = false;
        const char* text = nullptr;

        static LogArg Int(std::int64_t value);
        static LogArg Float(double value);
        static LogArg Bool(bool value);
        static LogArg String(const char* value); // nullptr は空の文字列
    };

    // 1件のログ。文は持たない(チャンネルと番号で定義を引いて、受け手が文にする)。
    // 決まった大きさなので、配列やリングバッファにそのまま並べられ、コピーしても中の文字列ごと写る(確保しない)。
    struct LogRecord
    {
        // 記録の中の1つの値。文字列は text の中の場所(先頭からのバイト数)で指す(ポインタだと、記録をコピーしたときに古い方を指してしまう)。
        struct Arg
        {
            LogArgType type = LogArgType::None;
            std::uint16_t textOffset = 0;
            std::int64_t intValue = 0;
            double floatValue = 0.0;
            bool boolValue = false;
        };

        double timeSeconds = 0.0;   // Log を作ってからの秒
        std::uint64_t frame = 0;    // AdvanceFrame を呼んだ回数(ゲームは StepFrame の最初に呼ぶ)
        std::uint32_t thread = 0;   // スレッドの番号(そのスレッドが最初にログを出したときに 1 から振る)
        LogLevel level = LogLevel::Info;
        std::uint16_t channel = 0;  // 登録の番号(実行中だけの数。外へ書くときは名前にする)
        std::uint16_t id = 0;       // チャンネルの中の番号(enum の値)。LogTextId なら ID の無いログ
        std::uint8_t argCount = 0;
        std::uint16_t textLength = 0; // text に使ったバイト数
        Arg args[MaxLogArgs];
        char text[MaxLogTextBytes] = {};

        // 値を足す(文字列は text へ写す。入りきらなければ UTF-8 の文字の途中で切らないように切る)。足せなければ false。
        bool AddArg(const LogArg& arg);
        // i 番目の文字列の値(文字列でなければ "")。
        const char* GetString(int index) const;
    };

    // ---- 定義(テンプレートを使わない形。C++ の表も、スクリプトも、これで登録する) ----

    struct LogArgDef
    {
        const char* name = nullptr;
        LogArgType type = LogArgType::None;
    };

    struct LogMessageDef
    {
        std::uint16_t id = 0; // 0 は使わない(ID の無いログ用に取っておく)
        const char* name = nullptr;
        LogLevel level = LogLevel::Info;
        const char* format = nullptr; // 文の型紙。{値の名前} が置き換わる
        LogArgDef args[MaxLogArgs] = {}; // 前から詰めて書く。種類が None のところで終わり

        // 値の数(前から数えて、種類が None でないもの)。
        constexpr int ArgCount() const
        {
            int count = 0;
            while (count < MaxLogArgs && args[count].type != LogArgType::None)
                ++count;
            return count;
        }
    };

    // 受け手。登録すると、出たログを1件ずつ受け取る(Log の mutex を持ったまま呼ばれる。中でログを出さない)。
    class LogSink
    {
    public:
        virtual ~LogSink() = default;
        virtual void Receive(const LogRecord& record) = 0;
    };

    // ログの窓口(プログラム全体で1つ。Get で取る)。
    class Log
    {
    public:
        static constexpr int MaxChannels = 256; // 親だけのチャンネル(App.Enemy.System の App と App.Enemy)も数える

        static Log& Get();

        // チャンネルを名前で登録し、番号を返す。同じ名前ならすでにある番号(C++ とスクリプトが同じチャンネルを使える)。
        // 名前は . でつないだ道筋(例 "App.Enemy.System")で、親(App、App.Enemy)も親子のつながりで登録する。
        // 区切りは英数字と _ だけ、空の区切りは不可。使えない名前・いっぱいなら -1(Log チャンネルのエラー)。名前は写して持つ。
        int RegisterChannel(const char* name);

        // メッセージを1つ登録する(文字列は写して持つ)。番号 0、同じチャンネルで番号が重なる、値が多すぎるときは
        // 登録せずに false(Log チャンネルのエラーも出す)。
        bool RegisterMessage(int channel, const LogMessageDef& message);

        // 1件出す(スクリプト用の、テンプレートを使わない形)。値の数と種類を定義と比べ、違えば出さずに false
        // (Log チャンネルのエラーを出す)。レベルは定義から取る。
        bool Write(int channel, std::uint16_t id, const LogArg* args, int argCount);

        // 1件出す(HUSKY_LOG が使う。定義との照らし合わせはコンパイルのときに済んでいる)。時刻・フレーム・スレッドはここで入れる。
        void WriteChecked(int channel, LogLevel level, std::uint16_t id, const LogArg* args, int argCount);

        // ID の無いログを1件出す(HUSKY_LOG_TEXT と、SDL のログ)。文そのものを、文字列の値1つとして持つ。
        void WriteText(int channel, LogLevel level, const char* text);

        // 受け手の付け外し(借りるだけ。外すまで壊さないこと)。
        void AddSink(LogSink* sink);
        void RemoveSink(LogSink* sink);

        // フレームの区切り(記録の frame を1つ進める)。
        void AdvanceFrame() { m_frame.fetch_add(1, std::memory_order_relaxed); }

        // 記録を文にする(受け手が使う。"[Warning][Asset] .meta がありません(foo.png)" の形)。確保しない。
        // 書いた文字数を返す(入れ物が足りなければ切る。終わりの 0 は必ず書く)。
        // 受け手の Receive の中(mutex を持っている)からも、外からも呼べる。
        std::size_t FormatRecord(const LogRecord& record, char* buffer, std::size_t size);

        // 定義を引く(無ければ nullptr)。返したポインタは、次の登録まで有効。
        const char* GetChannelName(int channel);
        int FindChannel(const char* name); // 無ければ -1
        int GetParentChannel(int channel); // 親の番号(いちばん上の段なら -1)
        // channel が ancestor そのものか、その下(子・孫…)にあるか。
        bool IsUnder(int channel, int ancestor);

        // HUSKY_ASSERT が外れたときに、デバッガで止めるか(テストは false にする。既定は true)。
        void SetBreakOnAssert(bool enabled) { m_breakOnAssert = enabled; }
        bool GetBreakOnAssert() const { return m_breakOnAssert; }

    private:
        Log();

        struct ArgInfo
        {
            std::string name;
            LogArgType type = LogArgType::None;
        };
        struct MessageInfo
        {
            bool registered = false;
            std::string name;
            LogLevel level = LogLevel::Info;
            std::string format;
            std::vector<ArgInfo> args;
        };
        struct ChannelInfo
        {
            std::string name; // 道筋の全体("App.Enemy.System")
            int parent = -1;
            std::vector<MessageInfo> messages; // 番号を添字にする(番号は小さな数を手で振るので、すき間は少ない)
        };

        void Deliver(int channel, LogLevel level, std::uint16_t id, const LogArg* args, int argCount); // mutex を持って呼ぶ
        std::size_t FormatLocked(const LogRecord& record, char* buffer, std::size_t size) const;
        int FindChannelLocked(const char* name, std::size_t length) const;

        std::recursive_mutex m_mutex; // FormatRecord を受け手の中からも呼べるように、同じスレッドなら二重に取れる mutex
        std::vector<ChannelInfo> m_channels;
        std::vector<LogSink*> m_sinks;
        LogRecord m_scratch; // Deliver が使い回す記録(mutex を持っている間だけ使う)
        std::atomic<std::uint64_t> m_frame{0};
        std::int64_t m_startNanoseconds = 0;
        bool m_breakOnAssert = true;
    };

    // 受け手が「このチャンネルから下は、このレベル以上だけ」を決める道具(入れ子のチャンネルの絞り込み)。
    // 決めていないチャンネルは、いちばん近い親の決めごとを使い、どこにも無ければ既定(SetDefault)。
    //   filter.SetLevel("Engine.Stats", LogLevel::Warning); // 毎秒の統計(Engine.Stats とその下)は Warning 以上だけ
    //   filter.SetLevel("App.Enemy", LogLevel::Verbose);    // App.Enemy の下は全部
    // 確保しない(チャンネルの番号を添字にした、決まった大きさの配列)。
    class LogChannelFilter
    {
    public:
        explicit LogChannelFilter(LogLevel defaultMinimum = LogLevel::Info);

        void SetDefault(LogLevel minimum) { m_default = minimum; }
        // チャンネルを登録してから決める(まだメッセージを出していないチャンネルにも決められる)。名前が使えなければ false。
        bool SetLevel(const char* channelPath, LogLevel minimum);
        void ClearLevel(const char* channelPath);
        // 記録を通すか。
        bool Passes(const LogRecord& record) const;

    private:
        static constexpr std::int8_t Unset = -1;
        std::int8_t m_levels[Log::MaxChannels]; // チャンネルごとの決めごと(LogLevel の数)。Unset は決めていない
        LogLevel m_default;
    };

    // 画面(標準出力)に1行ずつ出す受け手。既定は Info 以上(Verbose は毎秒の処理時間など、量が多いもの)。
    // チャンネルごとのレベルは GetFilter で決める。
    class ConsoleLogSink : public LogSink
    {
    public:
        explicit ConsoleLogSink(LogLevel minimum = LogLevel::Info)
            : m_filter(minimum)
        {
        }
        void Receive(const LogRecord& record) override;
        LogChannelFilter& GetFilter() { return m_filter; }

    private:
        LogChannelFilter m_filter;
    };

    // HUSKY_LOG_TEXT の中身(printf の形で文を作り、WriteText に渡す。文は MaxLogTextBytes - 1 バイトまで)。
    void WriteLogTextFormat(int channel, LogLevel level, const char* format, ...);

    // 受け手を、このオブジェクトが生きている間だけ付ける(RAII。壊れるときに外すので、外し忘れて壊れた受け手を呼ぶことがない)。
    //   core::ConsoleLogSink console;
    //   core::ScopedLogSink attach(console); // main を抜けるときに外れる
    class ScopedLogSink
    {
    public:
        explicit ScopedLogSink(LogSink& sink)
            : m_sink(&sink)
        {
            Log::Get().AddSink(m_sink);
        }
        ~ScopedLogSink() { Log::Get().RemoveSink(m_sink); }
        ScopedLogSink(const ScopedLogSink&) = delete;
        ScopedLogSink& operator=(const ScopedLogSink&) = delete;

    private:
        LogSink* m_sink;
    };

    // ---- C++ の包み(HUSKY_LOG のコンパイル時の照らし合わせ) ----

    // C++ の値の型 → LogArgType。対応しない型は None(static_assert で止まる)。
    template <typename T>
    constexpr LogArgType LogArgTypeOf()
    {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<U, bool>)
            return LogArgType::Bool;
        else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>)
            return LogArgType::Int;
        else if constexpr (std::is_floating_point_v<U>)
            return LogArgType::Float;
        else if constexpr (std::is_same_v<std::decay_t<U>, const char*> || std::is_same_v<std::decay_t<U>, char*> ||
                           std::is_same_v<U, std::string>)
            return LogArgType::String;
        else
            return LogArgType::None;
    }

    // C++ の値 → 記録の値。
    template <typename T>
    LogArg MakeLogArg(const T& value)
    {
        constexpr LogArgType type = LogArgTypeOf<T>();
        if constexpr (type == LogArgType::Bool)
            return LogArg::Bool(value);
        else if constexpr (type == LogArgType::Int)
            return LogArg::Int(static_cast<std::int64_t>(value)); // 整数と enum
        else if constexpr (type == LogArgType::Float)
            return LogArg::Float(static_cast<double>(value));
        else if constexpr (std::is_same_v<T, std::string>)
            return LogArg::String(value.c_str());
        else
            return LogArg::String(value); // const char* と文字列の定数(char の配列)
    }

    // 表から番号のメッセージを探す(コンパイルのときに使う)。
    template <std::size_t N>
    constexpr const LogMessageDef* FindLogMessageDef(const LogMessageDef (&messages)[N], std::uint16_t id)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (messages[i].id == id)
                return &messages[i];
        }
        return nullptr;
    }

    // 値の種類が定義と同じか(コンパイルのときに使う)。
    template <typename... Args>
    constexpr bool LogArgsMatch(const LogMessageDef& message)
    {
        // 先頭の None は、値が0個のときに配列の大きさが0にならないための埋め草。
        constexpr LogArgType types[] = {LogArgType::None, LogArgTypeOf<Args>()...};
        for (std::size_t i = 0; i < sizeof...(Args); ++i)
        {
            if (types[i + 1] != message.args[i].type)
                return false;
        }
        return true;
    }

    // C++ の表を窓口へまとめて登録し、チャンネルの番号を返す(HUSKY_LOG_CHANNEL_END が使う)。
    int RegisterLogChannelTable(const char* channelName, const LogMessageDef* messages, std::size_t count);

    // HUSKY_LOG の中身。Message は enum の値(テンプレートの引数なので、どのメッセージかがコンパイルのときに分かる)。
    template <auto Message, typename... Args>
    void WriteLog(const Args&... args)
    {
        using Enum = decltype(Message);
        // ADL(引数の型の名前空間から関数を探す)で、この enum の表を持つ構造体を見つける。
        using Channel = std::remove_pointer_t<decltype(HuskyLogChannelOf(static_cast<const Enum*>(nullptr)))>;
        constexpr const LogMessageDef* message = FindLogMessageDef(Channel::messages, static_cast<std::uint16_t>(Message));
        static_assert(message != nullptr, "HUSKY_LOG: この enum の値は HUSKY_LOG_MESSAGE で定義されていません");
        static_assert(message->ArgCount() == static_cast<int>(sizeof...(Args)), "HUSKY_LOG: 値の数が定義と違います");
        static_assert(LogArgsMatch<Args...>(*message), "HUSKY_LOG: 値の種類が定義と違います");

        // 値を並べる(畳み込み式: 引数の数だけくり返す)。先頭は値が0個のときに配列の大きさが0にならないための埋め草。
        const LogArg values[] = {LogArg{}, MakeLogArg(args)...};
        Log::Get().WriteChecked(Channel::Index(), message->level, message->id, values + 1,
                                static_cast<int>(sizeof...(Args)));
    }

    // HUSKY_ASSERT が外れたとき(開発ビルドだけ)。記録して、設定ならデバッガで止める。
    void ReportAssertFailure(const char* condition, const char* file, int line);
}

// 値の名前と種類(String / Int / Float / Bool)。
#define HUSKY_LOG_ARG(argName, argType) ::husky::core::LogArgDef{#argName, ::husky::core::LogArgType::argType}

// チャンネルの定義を始める。EnumType はチャンネルの enum、channelName は名前の文字列(属性にあたる)。
// 中では構造体 HuskyLogChannel_EnumType を作り、ADL で見つける関数 HuskyLogChannelOf を置く。
#define HUSKY_LOG_CHANNEL_BEGIN(EnumType, channelName)                                                        \
    struct HuskyLogChannel_##EnumType;                                                                        \
    constexpr HuskyLogChannel_##EnumType* HuskyLogChannelOf(const EnumType*) { return nullptr; }              \
    struct HuskyLogChannel_##EnumType                                                                         \
    {                                                                                                         \
        using ThisEnum = EnumType;                                                                            \
        static constexpr const char* name = channelName;                                                      \
        static constexpr ::husky::core::LogMessageDef messages[] = {

// メッセージを1つ定義する。level は Verbose / Info / Warning / Error。値は HUSKY_LOG_ARG で 0〜8 個。
#define HUSKY_LOG_MESSAGE(valueName, level, format, ...)                                                      \
    ::husky::core::LogMessageDef{static_cast<std::uint16_t>(ThisEnum::valueName), #valueName,                 \
                                 ::husky::core::LogLevel::level, format, {__VA_ARGS__}},

// チャンネルの定義を終える。登録は最初に使うとき(Index)と、プログラムの始まり(registered。カタログに全部が載るように)。
#define HUSKY_LOG_CHANNEL_END()                                                                               \
        };                                                                                                    \
        static int Index()                                                                                    \
        {                                                                                                     \
            static const int index = ::husky::core::RegisterLogChannelTable(                                  \
                name, messages, sizeof(messages) / sizeof(messages[0]));                                      \
            return index;                                                                                     \
        }                                                                                                     \
        static inline const int registered = Index();                                                         \
    };

// ログを1件出す。message はチャンネルの enum の値(AssetLog::MetaMissing)、続けて値。
#define HUSKY_LOG(message, ...) ::husky::core::WriteLog<message>(__VA_ARGS__)

// ID の無いログ(その場限りのデバッグ用。テレメトリで絞り込みたいものは HUSKY_LOG にする)。
//   HUSKY_LOG_TEXT(Info, "Debug", "x = %d", x);
// level は Verbose / Info / Warning / Error、channelName は名前の文字列、続けて printf の形の書式と値。
// チャンネルの番号は、この行を最初に通ったときに1回だけ引く(static)。
#define HUSKY_LOG_TEXT(level, channelName, ...)                                                               \
    do                                                                                                        \
    {                                                                                                         \
        static const int huskyLogTextChannel = ::husky::core::Log::Get().RegisterChannel(channelName);       \
        ::husky::core::WriteLogTextFormat(huskyLogTextChannel, ::husky::core::LogLevel::level, __VA_ARGS__);  \
    } while (false)

// 条件が成り立つことを確かめる。外れたら Assert のレベルで記録し、開発ビルドではデバッガで止める。
// 製品(Release)ではコードごと抜く。条件も計算しないので、条件に副作用(値を書き換える処理)を書かない。
#if HUSKY_DEVELOPMENT
#define HUSKY_ASSERT(condition)                                                                               \
    do                                                                                                        \
    {                                                                                                         \
        if (!(condition))                                                                                     \
            ::husky::core::ReportAssertFailure(#condition, __FILE__, __LINE__);                               \
    } while (false)
#else
// sizeof の中は計算されない。条件の中の変数を「使った」ことにして、使っていない変数の警告を防ぐ。
#define HUSKY_ASSERT(condition) ((void)sizeof(!(condition)))
#endif
