// 決まった刻みの時計(PHY-1)。画面のフレームの長さ(可変の dt)を貯めて、決まった長さ(既定は 1/60 秒)ずつ物理を進める。
//
// なぜ決まった刻みにするか: 物理の計算は、1 歩の長さが変わると結果が変わる(速いものが壁を抜ける、積み重ねが揺れる、
// 同じ操作でも毎回違う結果になる)。画面が 145 FPS でも 30 FPS でも、物理はいつも同じ長さで進めると、結果がそろう。
//
// 使い方(1 フレームに 1 回):
//   const int steps = stepper.Advance(dt);    // このフレームで進める歩数(0 のこともある)
//   for (int i = 0; i < steps; ++i) { world.Step(stepper.GetStepSeconds()); }
//   const double alpha = stepper.GetAlpha();   // 貯まっている余り(次の 1 歩のどこまで来たか、0〜1)。描くときの補間に使う
//
// 1 フレームに進める歩数には上限がある(既定 4)。重くて歩数が足りないとき、貯まり続けると、
// 次のフレームはもっと多く進める → もっと重くなる、と止まらなくなる(「死のスパイラル」)。上限を超えた分は捨てる
// (そのときはゲームの中の時間がゆっくりになる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

namespace husky::engine
{
    class FixedStepper
    {
    public:
        explicit FixedStepper(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 4);

        // dt 秒を貯め、進める歩数を返す。貯まった分から、その歩数ぶんを引く。
        int Advance(double dt);

        double GetStepSeconds() const { return m_stepSeconds; }
        int GetMaxStepsPerFrame() const { return m_maxStepsPerFrame; }
        // 貯まっている余りが、1 歩の何割か(0 以上 1 未満)。
        double GetAlpha() const { return m_accumulated / m_stepSeconds; }
        // 貯まった分を捨てる(再生を始め直すときなど)。
        void Reset() { m_accumulated = 0.0; }

    private:
        double m_stepSeconds;
        int m_maxStepsPerFrame;
        double m_accumulated = 0.0; // まだ進めていない時間(秒)
    };
}
