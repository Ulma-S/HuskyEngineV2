// Profiler.h の実装。
#include "core/Profiler.h"

#include <cstring> // std::strcmp

namespace husky::core
{
    namespace
    {
        // 平均をなめらかにする割合: 新しいフレームの値を 5% だけ混ぜる(だいたい直近 20 フレームの平均のように動く)。
        constexpr double SmoothingFactor = 0.05;
    }

    Profiler& Profiler::Get()
    {
        // 関数の中の static 変数: 最初にここを通ったときに1回だけ作られ、プログラムが終わるまで残る。
        static Profiler instance;
        return instance;
    }

    int Profiler::RegisterSection(const char* name)
    {
        for (int i = 0; i < m_count; ++i)
        {
            if (std::strcmp(m_stats[i].name, name) == 0)
            {
                return i; // 同じ名前は同じ区間(別の場所で測った時間も合わせて足す)
            }
        }
        if (m_count >= MaxSections)
        {
            return -1;
        }
        m_stats[m_count] = SectionStats{};
        m_stats[m_count].name = name;
        m_currentNs[m_count] = 0;
        m_currentCalls[m_count] = 0;
        return m_count++;
    }

    void Profiler::Add(int section, std::int64_t nanoseconds)
    {
        if (section < 0 || section >= m_count)
        {
            return;
        }
        m_currentNs[section] += nanoseconds;
        ++m_currentCalls[section];
    }

    void Profiler::EndFrame()
    {
        for (int i = 0; i < m_count; ++i)
        {
            SectionStats& stats = m_stats[i];
            stats.lastMs = static_cast<double>(m_currentNs[i]) / 1.0e6; // ナノ秒 → ミリ秒
            stats.lastCalls = m_currentCalls[i];
            stats.averageMs += (stats.lastMs - stats.averageMs) * SmoothingFactor;
            if (stats.lastMs > stats.maxMs)
            {
                stats.maxMs = stats.lastMs;
            }
            m_currentNs[i] = 0;
            m_currentCalls[i] = 0;
        }
    }

    void Profiler::ResetMax()
    {
        for (int i = 0; i < m_count; ++i)
        {
            m_stats[i].maxMs = 0.0;
        }
    }
}
