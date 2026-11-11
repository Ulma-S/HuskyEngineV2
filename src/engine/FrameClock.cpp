// FrameClock.h の実装。OS や SDL には触らない(時刻は引数でもらう)。
#include "engine/FrameClock.h"

namespace husky::engine
{
    FrameClock::FrameClock(double maxDeltaSeconds)
        : m_maxDeltaSeconds(maxDeltaSeconds)
    {
    }

    double FrameClock::Tick(std::uint64_t nowNanoseconds)
    {
        double delta = 0.0;

        if (m_hasPrevious)
        {
            // 時刻の差は整数のまま取ってから秒に直す。
            // 時刻そのものを double にすると、起動からの時間が長いほど細かい桁が失われるため。
            const std::uint64_t elapsedNanoseconds = nowNanoseconds - m_previousNanoseconds;
            delta = static_cast<double>(elapsedNanoseconds) / 1'000'000'000.0; // 1'000'000'000 は C++14 からの桁区切り
        }
        m_previousNanoseconds = nowNanoseconds;
        m_hasPrevious = true;

        // テスト用の固定値が設定されていれば、実時間より優先する。
        if (m_fixedDeltaSeconds > 0.0)
        {
            delta = m_fixedDeltaSeconds;
        }

        // 上限でクランプ(切り詰め)する。
        if (delta > m_maxDeltaSeconds)
        {
            delta = m_maxDeltaSeconds;
        }

        m_deltaSeconds = delta;
        return delta;
    }

    void FrameClock::SetFixedDelta(double seconds)
    {
        m_fixedDeltaSeconds = seconds;
    }
}
