// ゲームの HUD(UI-7。ゲーム用 UI の例): スコアと残り時間を、UI の文字(UITextComponent)に書く。
// HUD のプレハブ(assets/prefabs/hud.prefab)の Canvas の根に付いている。「何を数え、どう見せるか」はゲームの決め事なので、
// エンジンではなくゲームのコードに置く。
//   - スコア: プレイヤー(PlayerActor::score)の、最後のリセットからの増え方
//   - 残り時間: 再生中に timeLimit 秒から減る。0 になったら「おしまい!」と出し、スコアもそこで止める
//   - ResetGame: スコアと残り時間を最初に戻す。リセットのボタン(UIButtonComponent)から名前で呼ばれる(HUSKY_METHOD)
// 文字は変わったときだけ書き換える(毎フレーム文字列を作らない)。
// 動作スレッド: ゲームスレッド
#pragma once

#include "core/ObjectRef.h"
#include "engine/Component.h"
#include "engine/UIComponents.h"

#include <string>

namespace husky::game
{
    class HudComponent : public engine::Component
    {
        HUSKY_OBJECT(HudComponent, engine::Component)
    public:
        core::ObjectRef<engine::UITextComponent> scoreText; // スコアを書く文字(プレハブの中の子)
        core::ObjectRef<engine::UITextComponent> timeText;  // 残り時間を書く文字
        float timeLimit = 60.0f;                            // 1回の長さ(秒)

        void Tick(double dt) override;

        // スコアと残り時間を最初に戻す(リセットのボタンから呼ばれる)。
        void ResetGame();

        int GetScore() const { return m_score; }
        float GetRemainingSeconds() const { return m_remaining; }
        bool IsFinished() const { return m_started && m_remaining <= 0.0f; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(scoreText)
            HUSKY_PROPERTY(timeText)
            HUSKY_PROPERTY(timeLimit)
            HUSKY_METHOD(ResetGame)
        HUSKY_REFLECTION_END()

    private:
        // シーンの最初のプレイヤーの今の score(いなければ 0)。見つけたプレイヤーは弱い参照で覚える。
        int ReadPlayerScore();
        // 文字の部品に書く(値が変わったときだけ)。
        void UpdateTexts();

        bool m_started = false;   // 最初の Tick で残り時間を timeLimit にする
        float m_remaining = 0.0f; // 残り時間(秒)
        int m_score = 0;          // 表示しているスコア
        int m_scoreBase = 0;      // リセットしたときのプレイヤーの score
        int m_shownScore = -1;    // 最後に書いたスコア(-1 はまだ書いていない)
        int m_shownSeconds = -1;  // 最後に書いた残りの秒(-1 はまだ書いていない)
        core::ObjectHandle m_player;
    };
}
