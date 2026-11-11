// ゲームから見たキーボードの状態。
// 「押されているか」「このフレームで押されたか」「このフレームで離されたか」を答える。
#pragma once

#include "platform/Keyboard.h"

#include <array>  // std::array(大きさが固定の配列)
#include <vector>

namespace husky::engine
{
    using platform::Key; // engine の中では platform::Key を Key と書けるようにする

    // OS には触らず、platform から渡されたキーイベントだけで状態を作る。
    // そのため、テストでは偽のイベントを渡すだけで入力を再現できる。
    // 動作スレッド: ゲームスレッド
    class Input final
    {
    public:
        // 毎フレーム、更新の前に1回呼ぶ。このフレームに起きたキーイベントを、起きた順に渡す。
        // const & は「コピーせずに受け取るが、中身は書き換えない」という意味。
        void BeginFrame(const std::vector<platform::KeyEvent>& keyEvents);

        // 今、押されているか。
        bool IsDown(Key key) const;
        // このフレームで押されたか(押した瞬間の1フレームだけ true)。
        bool WasPressed(Key key) const;
        // このフレームで離されたか(離した瞬間の1フレームだけ true)。
        bool WasReleased(Key key) const;

    private:
        // Key を配列の番号に変換する。
        static std::size_t ToIndex(Key key) { return static_cast<std::size_t>(key); }

        // キーごとの状態。{} は「全部 false で初期化する」という意味。
        std::array<bool, platform::KeyCount> m_isDown{};
        std::array<bool, platform::KeyCount> m_wasPressed{};
        std::array<bool, platform::KeyCount> m_wasReleased{};
    };
}
