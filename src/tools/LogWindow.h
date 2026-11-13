// ログの窓(ImGui。LOG-4): エンジンのログを、開発ビルドのエディタの中で見る。製品(Release)には入らない。
//
//   - レベル(Verbose / Info / Warning / Error / Assert)の表示を切り替える(Verbose は既定で隠す。毎秒の処理時間など量が多い)
//   - チャンネルを選ぶと、その下(子・孫…)だけを出す(入れ子のチャンネル。"Engine" を選ぶと Engine.Asset なども出る)
//   - 番号(ID)で絞る(0 なら全部)、文字で探す(文の中に含むもの)
//   - Clear で消す、Auto-scroll で最新の行を追う
//
// 仕組み:
//   - 受け手(LogSink)として Log に付け、受け取ったときに文にして、決まった数の行の入れ物(リングバッファ)に写す。
//     文にするのは受け取ったとき(Log の mutex の中)。描くときに Log の mutex を取らないので、
//     「この窓の mutex → Log の mutex」の逆の順にならず、2つの mutex で止まることが無い。
//   - 入れ物は作るときに1回だけ確保する。毎フレームの描画は、温まった後は確保しない(見える行の番号の並びも使い回す)。
//
// 使い方(main.cpp):
//   tools::LogWindow logWindow;
//   core::ScopedLogSink attachLogWindow(logWindow); // 窓より後に作り、先に壊す
//   ... logWindow.Draw();                            // 開発用の UI の中で毎フレーム
//
// 動作スレッド: Receive はどのスレッドからでも。Draw と設定の関数はゲームスレッドのみ。Draw の中でログを出さないこと。
#pragma once
#if HUSKY_DEVELOPMENT
#include "core/Log.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace husky::tools
{
    class LogWindow : public core::LogSink
    {
    public:
        static constexpr std::size_t DefaultCapacity = 2000; // 覚えておく行の数(古いものから消える)
        static constexpr std::size_t MaxLineBytes = 320;     // 1行の文の大きさ(終わりの 0 を含む。長ければ切る)

        explicit LogWindow(std::size_t capacity = DefaultCapacity);

        void Receive(const core::LogRecord& record) override;

        // 窓を描く(ImGui)。
        void Draw();

        // ---- 絞り込み(画面の部品もこれを使う。テストからも呼べる) ----
        void SetLevelShown(core::LogLevel level, bool shown);
        bool IsLevelShown(core::LogLevel level) const;
        // チャンネルの道筋("Engine" など)。その下も含む。空なら全部。登録されていない名前なら何も出ない。
        void SetChannelFilter(const std::string& channelPath);
        void SetIdFilter(int id) { m_idFilter = id; m_visibleDirty = true; } // 0 なら全部
        void SetSearch(const std::string& text);
        void Clear();

        // 今の絞り込みで見える行の数と、i 番目の行の文(テストと確認用)。見える行は RefreshVisible で作り直す。
        void RefreshVisible();
        std::size_t GetVisibleCount() const { return m_visible.size(); }
        std::string GetVisibleText(std::size_t index);
        // レベルごとの数(消すまでの合計。古くて入れ物から消えた行も数える)。
        std::uint64_t GetCount(core::LogLevel level);

    private:
        struct Line
        {
            core::LogLevel level = core::LogLevel::Info;
            std::uint16_t channel = 0;
            std::uint16_t id = 0;
            std::uint64_t serial = 0; // 受け取った順の通し番号(1 から)
            char text[MaxLineBytes] = {};
        };

        const Line& LineAt(std::size_t order) const; // 古い方から order 番目(m_mutex を持って呼ぶ)
        bool Matches(const Line& line) const;        // 絞り込みに合うか(m_mutex を持って呼ぶ)
        void RefreshChannelMatch();                  // チャンネルの絞り込みの表を作り直す(Log の mutex を取る)

        std::mutex m_mutex; // m_lines、m_head、m_count、m_counts、m_serial を守る
        std::vector<Line> m_lines; // リングバッファ
        std::size_t m_head = 0;    // いちばん古い行の場所
        std::size_t m_count = 0;
        std::uint64_t m_serial = 0;
        std::uint64_t m_counts[5] = {}; // レベルごとの数

        // 絞り込み(ゲームスレッドだけが触る)
        bool m_levelShown[5] = {false, true, true, true, true}; // Verbose だけ既定で隠す
        std::string m_channelFilter;
        bool m_channelMatch[core::Log::MaxChannels] = {}; // チャンネルの番号ごとに、絞り込みに合うか
        int m_channelCountAtRefresh = -1;
        int m_idFilter = 0;
        std::string m_search;
        bool m_autoScroll = true;

        // 見える行(古い方からの順番 order の並び。毎回作り直すが、入れ物は使い回す)
        std::vector<std::uint32_t> m_visible;
        bool m_visibleDirty = true;
        std::uint64_t m_serialAtRefresh = ~std::uint64_t{0};
        std::uint64_t m_headSerialAtRefresh = 0;

        // 画面の部品の入れ物(ImGui の文字の欄。確保しないように決まった大きさ)
        char m_searchInput[128] = {};
        int m_frame = 0;
    };
}
#endif
