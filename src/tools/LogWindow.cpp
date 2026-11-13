// ログの窓(LOG-4)。仕組みは LogWindow.h の最初のコメント。
#if HUSKY_DEVELOPMENT // エディタの機能は開発ビルドだけ(製品からコードごと外す)

#include "tools/LogWindow.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace husky::tools
{
    namespace
    {
        int LevelIndex(core::LogLevel level) { return static_cast<int>(level); }

        // レベルごとの文字の色(Verbose は灰色、Warning は黄色、Error と Assert は赤)。Info は既定の色(nullptr)。
        const ImVec4* LevelColor(core::LogLevel level)
        {
            static const ImVec4 gray(0.60f, 0.60f, 0.60f, 1.0f);
            static const ImVec4 yellow(1.00f, 0.85f, 0.30f, 1.0f);
            static const ImVec4 red(1.00f, 0.40f, 0.40f, 1.0f);
            switch (level)
            {
            case core::LogLevel::Verbose: return &gray;
            case core::LogLevel::Warning: return &yellow;
            case core::LogLevel::Error:
            case core::LogLevel::Assert: return &red;
            default: return nullptr;
            }
        }
    }

    LogWindow::LogWindow(std::size_t capacity)
        : m_lines(capacity > 0 ? capacity : 1) // ここで1回だけ確保する
    {
        m_visible.reserve(m_lines.size());
        for (bool& match : m_channelMatch)
            match = true;
    }

    void LogWindow::Receive(const core::LogRecord& record)
    {
        // 文にするのは受け取ったとき(ここは Log の mutex の中なので、FormatRecord が同じ mutex をもう一度取れる)。
        char text[MaxLineBytes];
        core::Log::Get().FormatRecord(record, text, sizeof(text));

        std::lock_guard<std::mutex> lock(m_mutex);
        Line* line = nullptr;
        if (m_count < m_lines.size())
        {
            line = &m_lines[(m_head + m_count) % m_lines.size()];
            ++m_count;
        }
        else
        {
            // いっぱい: いちばん古い行に上書きする。
            line = &m_lines[m_head];
            m_head = (m_head + 1) % m_lines.size();
        }
        line->level = record.level;
        line->channel = record.channel;
        line->id = record.id;
        line->serial = ++m_serial;
        std::memcpy(line->text, text, sizeof(text));
        ++m_counts[LevelIndex(record.level)];
    }

    void LogWindow::SetLevelShown(core::LogLevel level, bool shown)
    {
        m_levelShown[LevelIndex(level)] = shown;
        m_visibleDirty = true;
    }

    bool LogWindow::IsLevelShown(core::LogLevel level) const { return m_levelShown[LevelIndex(level)]; }

    void LogWindow::SetChannelFilter(const std::string& channelPath)
    {
        m_channelFilter = channelPath;
        m_channelCountAtRefresh = -1; // 表を作り直す
        m_visibleDirty = true;
    }

    void LogWindow::SetSearch(const std::string& text)
    {
        m_search = text;
        std::snprintf(m_searchInput, sizeof(m_searchInput), "%s", text.c_str()); // 画面の欄にも写す
        m_visibleDirty = true;
    }

    void LogWindow::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_head = 0;
        m_count = 0;
        for (std::uint64_t& count : m_counts)
            count = 0;
        m_visibleDirty = true;
    }

    std::uint64_t LogWindow::GetCount(core::LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_counts[LevelIndex(level)];
    }

    const LogWindow::Line& LogWindow::LineAt(std::size_t order) const
    {
        return m_lines[(m_head + order) % m_lines.size()];
    }

    bool LogWindow::Matches(const Line& line) const
    {
        if (!m_levelShown[LevelIndex(line.level)])
            return false;
        if (line.channel >= core::Log::MaxChannels || !m_channelMatch[line.channel])
            return false;
        if (m_idFilter != 0 && line.id != m_idFilter)
            return false;
        if (!m_search.empty() && std::strstr(line.text, m_search.c_str()) == nullptr)
            return false;
        return true;
    }

    void LogWindow::RefreshChannelMatch()
    {
        // チャンネルの木を引くので Log の mutex を取る。この窓の mutex は持たずに呼ぶ(逆の順で止まらないように)。
        core::Log& log = core::Log::Get();
        const int count = log.GetChannelCount();
        if (count == m_channelCountAtRefresh)
            return; // チャンネルが増えていなければ、表はそのまま
        m_channelCountAtRefresh = count;
        const int filter = m_channelFilter.empty() ? -1 : log.FindChannel(m_channelFilter.c_str());
        for (int i = 0; i < core::Log::MaxChannels; ++i)
        {
            if (m_channelFilter.empty())
                m_channelMatch[i] = true;
            else
                m_channelMatch[i] = filter >= 0 && i < count && log.IsUnder(i, filter); // 登録されていない名前なら何も出ない
        }
        m_visibleDirty = true;
    }

    void LogWindow::RefreshVisible()
    {
        RefreshChannelMatch();
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint64_t headSerial = m_count > 0 ? LineAt(0).serial : 0;
        if (!m_visibleDirty && m_serial == m_serialAtRefresh && headSerial == m_headSerialAtRefresh)
            return; // 新しい行も絞り込みの変化も無い
        m_serialAtRefresh = m_serial;
        m_headSerialAtRefresh = headSerial;
        m_visibleDirty = false;
        m_visible.clear(); // 入れ物は手放さない(確保しない)
        for (std::size_t order = 0; order < m_count; ++order)
        {
            if (Matches(LineAt(order)))
                m_visible.push_back(static_cast<std::uint32_t>(order));
        }
    }

    std::string LogWindow::GetVisibleText(std::size_t index)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index >= m_visible.size() || m_visible[index] >= m_count)
            return {};
        return LineAt(m_visible[index]).text;
    }

    void LogWindow::Draw()
    {
        ++m_frame;
        if (m_frame % 30 == 0)
            m_channelCountAtRefresh = -1; // ときどきチャンネルの表を作り直す(後から登録されたチャンネルのため)

        ImGui::SetNextWindowPos(ImVec2(300.0f, 500.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(640.0f, 210.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Log"))
        {
            ImGui::End();
            return;
        }

        // 1行目: レベルごとの表示の切り替えと数。
        for (int i = 0; i < 5; ++i)
        {
            const core::LogLevel level = static_cast<core::LogLevel>(i);
            if (i > 0)
                ImGui::SameLine();
            if (ImGui::Checkbox(core::ToString(level), &m_levelShown[i]))
                m_visibleDirty = true;
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled("(%llu)", static_cast<unsigned long long>(GetCount(level)));
        }

        // 2行目: チャンネル(選ぶとその下だけ)、番号、文字で探す、消す、最新を追う。
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::BeginCombo("Channel", m_channelFilter.empty() ? "(all)" : m_channelFilter.c_str()))
        {
            if (ImGui::Selectable("(all)", m_channelFilter.empty()))
                SetChannelFilter(std::string());
            core::Log& log = core::Log::Get();
            const int count = log.GetChannelCount();
            for (int i = 0; i < count; ++i)
            {
                const char* name = log.GetChannelName(i);
                if (name != nullptr && ImGui::Selectable(name, m_channelFilter == name))
                    SetChannelFilter(name);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("ID", &m_idFilter))
        {
            if (m_idFilter < 0)
                m_idFilter = 0;
            m_visibleDirty = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("Search", m_searchInput, sizeof(m_searchInput)))
        {
            m_search.assign(m_searchInput); // 打ったときだけ(入れ物の大きさが足りていれば確保しない)
            m_visibleDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);
        ImGui::Separator();

        // 行の一覧。見えている所だけを描く(ImGuiListClipper)ので、行が多くても軽い。
        RefreshVisible();
        ImGui::BeginChild("lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(m_mutex); // 描いている間に、ほかのスレッドが行を書き換えないように
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    if (m_visible[row] >= m_count)
                        continue; // Clear の直後など
                    const Line& line = LineAt(m_visible[row]);
                    const ImVec4* color = LevelColor(line.level);
                    if (color != nullptr)
                        ImGui::PushStyleColor(ImGuiCol_Text, *color);
                    ImGui::TextUnformatted(line.text);
                    if (color != nullptr)
                        ImGui::PopStyleColor();
                }
            }
        }
        // 最新の行を追う(いちばん下にいるときだけ。上を読んでいる間は動かさない)。
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }
}

#endif
