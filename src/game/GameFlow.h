// ゲームの流れ(SC-5、2026-11-14): タイトル → ゲーム → リザルト → タイトル …。
// シーンの形(ユーザーの案): ルートの master の下に、常駐の system と、入れ替える title / game / result。
//
//   master(ルートシーン。起動とエディタで開く)
//   ├─ system … 常駐。GameFlowComponent(状態と、最後のスコア)と BGM
//   ├─ title  … タイトルの文字と Start ボタン(GameFlowButtonComponent: StartGame)
//   ├─ game   … GameSpawnerComponent が、最初の Tick で今までのゲーム(跳ね回るもの・木箱・プレイヤー・カメラ・HUD)を自分のシーンに置く
//   └─ result … スコアの文字(ResultComponent)と「タイトルへ」ボタン(GameFlowButtonComponent: BackToTitle)
//
// シーンをまたぐ参照は保存できないので、ボタンや HUD は自分のシーンのコンポーネントを指し、system の GameFlowComponent は
// 実行中に World::FindComponent で探す。実行中にルートシーンは替えず、子のシーンを読む・下ろすだけ(World::LoadScene / UnloadScene)。
#pragma once

#include "core/EnumReflection.h"
#include "core/ObjectRef.h"
#include "engine/Component.h"
#include "engine/UIComponents.h"

#include <string>

namespace husky::engine
{
    class World;
}

namespace husky::game
{
    // ゲームの状態。
    enum class GameFlowState
    {
        Title,
        Playing,
        Result,
    };
    HUSKY_ENUM_DECLARE(GameFlowState)

    // 常駐の system のシーンに置く、ゲームの状態の係。状態に合わせて子のシーンを読み替える。
    class GameFlowComponent : public engine::Component
    {
        HUSKY_OBJECT(GameFlowComponent, engine::Component)
    public:
        GameFlowState state = GameFlowState::Title; // 今の状態(master を開いた直後は title が読まれているので Title)
        std::string titleScene = "title";           // それぞれの状態で読むシーンの名前(シーンの一覧の名前)
        std::string gameScene = "game";
        std::string resultScene = "result";

        // タイトル → ゲーム(title を下ろして game を読む)。Title のときだけ。
        void StartGame();
        // ゲーム → リザルト(スコアを覚え、game を下ろして result を読む)。Playing のときだけ。HUD が時間切れで呼ぶ。
        void FinishGame(int score);
        // リザルト → タイトル。Result のときだけ。
        void BackToTitle();

        // 最後に終えたゲームのスコア(保存しない。リザルトの画面が読む)。
        int GetLastScore() const { return m_lastScore; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(state)
            HUSKY_PROPERTY(titleScene)
            HUSKY_PROPERTY(gameScene)
            HUSKY_PROPERTY(resultScene)
            HUSKY_METHOD(StartGame)
            HUSKY_METHOD(BackToTitle)
        HUSKY_REFLECTION_END()

    private:
        // from を下ろして to を読む(どちらもフレームの物理の後に行われる)。
        void Switch(const std::string& from, const std::string& to);

        int m_lastScore = 0;
    };

    // ボタンが押されたときに、system の GameFlowComponent を探して呼ぶ係(ボタンと同じアクターに付ける)。
    // UIButtonComponent の呼び先を「自分のアクターのこのコンポーネントの Invoke」にする(同じシーンなので保存できる)。
    enum class GameFlowAction
    {
        StartGame,
        BackToTitle,
    };
    HUSKY_ENUM_DECLARE(GameFlowAction)

    class GameFlowButtonComponent : public engine::Component
    {
        HUSKY_OBJECT(GameFlowButtonComponent, engine::Component)
    public:
        GameFlowAction action = GameFlowAction::StartGame;

        void Invoke();

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(action)
            HUSKY_METHOD(Invoke)
        HUSKY_REFLECTION_END()
    };

    // リザルトのシーンに置く。最初の Tick で、最後のスコアを文字に書く。
    class ResultComponent : public engine::Component
    {
        HUSKY_OBJECT(ResultComponent, engine::Component)
    public:
        core::ObjectRef<engine::UITextComponent> scoreText; // スコアを書く文字(同じシーンの中)

        void Tick(double dt) override;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(scoreText)
        HUSKY_REFLECTION_END()

    private:
        bool m_written = false;
    };

    // ゲームのシーンに置く。最初の Tick で、今までのゲームの配置(SpawnGameplay)と HUD を置き、自分のシーンへ移す。
    // 編集中は Tick が止まるので、エディタでは置く係だけが見える(配置は乱数の種で毎回同じ)。
    class GameSpawnerComponent : public engine::Component
    {
        HUSKY_OBJECT(GameSpawnerComponent, engine::Component)
    public:
        void Tick(double dt) override;

    private:
        bool m_spawned = false;
    };

    // ワールドの GameFlowComponent(system のシーンのもの)。無ければ nullptr。
    GameFlowComponent* FindGameFlow(engine::World& world);
}
