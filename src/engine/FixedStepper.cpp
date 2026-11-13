// FixedStepper.h の実装。
#include "engine/FixedStepper.h"


namespace husky::engine
{
    FixedStepper::FixedStepper(double stepSeconds, int maxStepsPerFrame)
        : m_stepSeconds(stepSeconds > 0.0 ? stepSeconds : 1.0 / 60.0),
          m_maxStepsPerFrame(maxStepsPerFrame > 0 ? maxStepsPerFrame : 1)
    {
    }

    int FixedStepper::Advance(double dt)
    {
        if (dt > 0.0)
        {
            m_accumulated += dt;
        }
        // 小数の誤差の許し: 1/60 を足しても、2 進数では 1/60 ちょうどにならないことがある。
        // 「ほぼ 1 歩分」貯まっていれば 1 歩と数える(dt = 1/60 で毎回 1 歩になるように)。
        const double epsilon = m_stepSeconds * 1.0e-6;
        int steps = 0;
        while (m_accumulated + epsilon >= m_stepSeconds && steps < m_maxStepsPerFrame)
        {
            m_accumulated -= m_stepSeconds;
            ++steps;
        }
        if (m_accumulated < 0.0)
        {
            m_accumulated = 0.0; // 誤差の許しで少しマイナスになった分
        }
        if (steps == m_maxStepsPerFrame && m_accumulated >= m_stepSeconds)
        {
            // 上限まで進めてもまだ 1 歩分以上残っている: 追いつけないので、残りは捨てる(ゲームの中の時間がそのぶん遅れる)
            m_accumulated = 0.0;
        }
        return steps;
    }
}
