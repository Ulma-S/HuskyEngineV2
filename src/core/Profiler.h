// 簡単な CPU プロファイラー(処理のどこに時間がかかっているかを、区間ごとに測る)。
//
// 使い方: 測りたいブロックの最初に1行書くだけ。ブロックを抜けるときに、自動で測り終える。
//   void PhysicsScene2D::Update(...)
//   {
//       HUSKY_PROFILE_SCOPE("Physics2D");
//       ...
//   }
// 結果(このフレーム、平均、最大)は、Profiler::Get() の GetStats で読む(Debug の窓と毎秒のログが表示する)。
//
// 仕組み:
//   - ProfileScope は、作られたとき(コンストラクタ)に時計を読み、壊されるとき(デストラクタ)にもう一度読んで差を足す。
//     C++ では、ブロックの中で作った変数は、ブロックを抜けるときに必ず壊される(return でも途中の break でも)。
//     これを使って「始まりと終わりを対にする」やり方を RAII と呼ぶ(C# の using (...) { } と同じ考え方)。
//   - 区間の番号は、HUSKY_PROFILE_SCOPE を書いた場所ごとに、最初に通ったときに1回だけ登録する(関数の中の static 変数)。
//     2回目からは番号を引くだけなので、名前で探す手間も、メモリの確保も無い。
//   - 同じ名前を別の場所で使うと、同じ区間に足される(合計の時間と回数になる)。
//
// 製品(Release)では、HUSKY_PROFILE_SCOPE は何もしない(測る処理ごと消える)。Profiler のクラス自体は残る(テストのため)。
// 区間は最大 MaxSections 個。名前は文字列の定数(ずっと残るもの)を渡すこと(ポインタを覚えるだけで、写さない)。
//
// 動作スレッド: ゲームスレッドのみ(ジョブの中では測らない)。
#pragma once

#include <chrono>
#include <cstdint>

namespace husky::core
{
    class Profiler
    {
    public:
        static constexpr int MaxSections = 32;

        // 1つの区間の結果(ミリ秒)。
        struct SectionStats
        {
            const char* name = nullptr;
            double lastMs = 0.0;    // 前のフレームの合計
            double averageMs = 0.0; // 平均(なめらかにした値。急な1フレームの山にふらつかない)
            double maxMs = 0.0;     // ResetMax からの最大
            int lastCalls = 0;      // 前のフレームで通った回数
        };

        // プログラム全体で1つの Profiler(最初に呼んだときに作られる)。
        static Profiler& Get();

        // 区間を登録し、番号を返す(同じ名前がすでにあれば、その番号)。いっぱいなら -1(その区間は測らない)。
        int RegisterSection(const char* name);

        // 区間 section に、かかった時間(ナノ秒)を1回分足す。
        void Add(int section, std::int64_t nanoseconds);

        // フレームの区切り: このフレームに足した時間を「前のフレーム」に移し、平均と最大を更新して、次のフレームのために 0 に戻す。
        // Application::StepFrame の最初に呼ぶ(前のフレームの描画まで含めて1フレームになる)。
        void EndFrame();

        // 最大を 0 に戻す(毎秒のログを出した後に呼ぶと、「この1秒の最大」になる)。
        void ResetMax();

        int GetSectionCount() const { return m_count; }
        const SectionStats& GetStats(int section) const { return m_stats[section]; }


    private:
        SectionStats m_stats[MaxSections];
        std::int64_t m_currentNs[MaxSections] = {}; // このフレームに足した時間
        int m_currentCalls[MaxSections] = {};
        int m_count = 0;
    };

    // ブロックの始まりから終わりまでの時間を測る(RAII)。ふつうは HUSKY_PROFILE_SCOPE で作る。
    class ProfileScope
    {
    public:
        explicit ProfileScope(int section)
            : m_section(section)
            , m_start(std::chrono::steady_clock::now()) // 時計を読む(steady_clock: 時刻を合わせ直しても戻らない時計)
        {
        }

        ~ProfileScope()
        {
            const auto elapsed = std::chrono::steady_clock::now() - m_start;
            Profiler::Get().Add(m_section, std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        }

        // コピーしない(1回の計測は1つだけ)。
        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;

    private:
        int m_section;
        std::chrono::steady_clock::time_point m_start;
    };
}

// 変数の名前に行番号をつなぐための、2段のマクロ(1段だと __LINE__ が数字に置き換わる前につながってしまう)。
#define HUSKY_PROFILE_CONCAT_INNER(a, b) a##b
#define HUSKY_PROFILE_CONCAT(a, b) HUSKY_PROFILE_CONCAT_INNER(a, b)

#if HUSKY_DEVELOPMENT
// このブロックの終わりまでを、区間 name として測る。番号の登録は、この行を最初に通ったときの1回だけ(static)。
#define HUSKY_PROFILE_SCOPE(name)                                                                                    \
    static const int HUSKY_PROFILE_CONCAT(huskyProfileSection, __LINE__) =                                           \
        ::husky::core::Profiler::Get().RegisterSection(name);                                                        \
    const ::husky::core::ProfileScope HUSKY_PROFILE_CONCAT(huskyProfileScope, __LINE__)(                             \
        HUSKY_PROFILE_CONCAT(huskyProfileSection, __LINE__))
#else
// 製品では何もしない(測る処理ごと消える)
#define HUSKY_PROFILE_SCOPE(name) ((void)0)
#endif
