// ゲームから見たキーボードとマウスの状態。
// 「押されているか」「このフレームで押されたか」「このフレームで離されたか」を答える。
#pragma once

#include "core/Vector2.h"
#include "platform/InputEvents.h"
#include "platform/Keyboard.h"
#include "platform/Mouse.h"

#include <array>  // std::array(大きさが固定の配列)
#include <vector>

namespace husky::engine
{
    using platform::Key;         // engine の中では platform::Key を Key と書けるようにする
    using platform::MouseButton; // 同じく MouseButton と書けるようにする

    // OS には触らず、platform から渡されたイベントだけで状態を作る。
    // そのため、テストでは偽のイベントを渡すだけで入力を再現できる(dt と同じく、結果は毎回同じ)。
    // 動作スレッド: ゲームスレッド
    class Input final
    {
    public:
        // 毎フレーム、更新の前に1回呼ぶ。このフレームに起きたキーとマウスのイベントを、起きた順に渡す。
        // 画面の大きさ(events.screenWidth / screenHeight)も覚える。
        // const & は「コピーせずに受け取るが、中身は書き換えない」という意味。
        void BeginFrame(const platform::InputEvents& events);
        // キーのイベントだけのフレーム(マウスのイベントは無し、画面の大きさは前のまま)。テストの書きやすさのため。
        void BeginFrame(const std::vector<platform::KeyEvent>& keyEvents);

        // --- キーボード ---

        // 今、押されているか。
        bool IsDown(Key key) const;
        // このフレームで押されたか(押した瞬間の1フレームだけ true)。
        bool WasPressed(Key key) const;
        // このフレームで離されたか(離した瞬間の1フレームだけ true)。
        bool WasReleased(Key key) const;

        // --- マウス(UI-1) ---

        // マウスの位置。窓の中の画面のピクセルで、左上が (0, 0)、右が +x、下が +y。
        // ワールドの位置(上が +y)が欲しいときは engine::FindMouseWorldPosition(CameraComponent.h)を使う。
        // まだ一度も動いていなければ (0, 0)。
        core::Vector2 GetMousePosition() const { return m_mousePosition; }

        // キーと同じ3つの問い。マウスを UI が使ったフレーム(ConsumeMouse)は、
        // IsMouseDown と WasMousePressed は false(UI のボタンを押したのに、ゲームでも撃ってしまわないように)。
        bool IsMouseDown(MouseButton button) const;
        bool WasMousePressed(MouseButton button) const;
        bool WasMouseReleased(MouseButton button) const;

        // このフレームに回ったホイールの量の合計(奥へ回すと +)。マウスを UI が使ったフレームは 0。
        float GetMouseWheel() const { return m_mouseConsumed ? 0.0f : m_mouseWheel; }

        // 画面の大きさ(ピクセル)。画面が無い(テストや CLI)ときは 0。
        float GetScreenWidth() const { return m_screenWidth; }
        float GetScreenHeight() const { return m_screenHeight; }

        // このフレームのマウスは UI が使った、という印を付ける(次の BeginFrame で外れる)。
        // 後のゲーム用 UI(UI-5/6)が、ボタンの上で押されたときに、シーンの更新より前に呼ぶ予定。
        // ImGui(開発ビルド)の分は、platform がイベントを渡さないことで済ませているので、ここは使わない。
        void ConsumeMouse() { m_mouseConsumed = true; }
        bool IsMouseConsumed() const { return m_mouseConsumed; }

    private:
        // キーのイベントで、キーの状態を更新する(1フレームだけの印を消してから)。
        void ApplyKeyEvents(const std::vector<platform::KeyEvent>& keyEvents);
        // マウスの1フレームだけの印(押した・離した・ホイール・UI が使った)を消す。
        void ClearMouseFrameState();

        // Key を配列の番号に変換する。
        static std::size_t ToIndex(Key key) { return static_cast<std::size_t>(key); }
        static std::size_t ToIndex(MouseButton button) { return static_cast<std::size_t>(button); }

        // キーごとの状態。{} は「全部 false で初期化する」という意味。
        std::array<bool, platform::KeyCount> m_isDown{};
        std::array<bool, platform::KeyCount> m_wasPressed{};
        std::array<bool, platform::KeyCount> m_wasReleased{};

        // マウスの状態。
        core::Vector2 m_mousePosition;
        std::array<bool, platform::MouseButtonCount> m_mouseIsDown{};
        std::array<bool, platform::MouseButtonCount> m_mouseWasPressed{};
        std::array<bool, platform::MouseButtonCount> m_mouseWasReleased{};
        float m_mouseWheel = 0.0f;
        bool m_mouseConsumed = false;

        float m_screenWidth = 0.0f;
        float m_screenHeight = 0.0f;
    };
}
