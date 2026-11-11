// 1フレームの経過時間(dt)を決めるクラス。
// dt を決めてよいのはこのクラスだけ(CLAUDE.md: dt の決め方はゲームループの1か所に集約する)。
#pragma once

#include <cstdint>

namespace husky::engine
{
    // 動作スレッド: ゲームスレッド
    class FrameClock final
    {
    public:
        // maxDeltaSeconds: dt の上限(秒)。
        //   ブレークポイントで止めた後などに dt が巨大になり、物体がワープするのを防ぐ。
        explicit FrameClock(double maxDeltaSeconds = 0.1);

        // 毎フレームの最初に1回呼び、そのフレームの dt(秒)を返す。
        // nowNanoseconds: 今の時刻(ナノ秒)。時計は自分で読まず、呼ぶ側から受け取る。
        //   こうすると、テストでは好きな時刻を渡すだけで動かせる。
        // 最初の1回は前の時刻がないので 0 を返す。
        double Tick(std::uint64_t nowNanoseconds);

        // テスト用: dt を固定値にする。seconds が 0 以下なら固定を解除し、実時間に戻す。
        void SetFixedDelta(double seconds);

        // 直前の Tick が返した dt(秒)。
        double GetDeltaSeconds() const { return m_deltaSeconds; }

    private:
        double m_maxDeltaSeconds;
        double m_fixedDeltaSeconds = 0.0; // 0 以下なら固定しない
        double m_deltaSeconds = 0.0;
        std::uint64_t m_previousNanoseconds = 0;
        bool m_hasPrevious = false;       // 最初の Tick がまだなら false
    };
}
