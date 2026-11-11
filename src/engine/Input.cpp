// Input.h の実装。
#include "engine/Input.h"

namespace husky::engine
{
    void Input::BeginFrame(const std::vector<platform::KeyEvent>& keyEvents)
    {
        // 「このフレームで押された/離された」は1フレームだけ有効なので、毎フレーム消す。
        // 「押されているか」は、離されるまで続くので消さない。
        m_wasPressed.fill(false);
        m_wasReleased.fill(false);

        // イベントを起きた順に反映する。
        // 1フレームの間に「押して離した」場合も、WasPressed と WasReleased の両方が true になり、取りこぼさない。
        // for (const auto& e : keyEvents) は C# の foreach にあたる。auto は型をコンパイラに推論させる(C# の var)。
        for (const auto& keyEvent : keyEvents)
        {
            const std::size_t index = ToIndex(keyEvent.key);
            if (keyEvent.isDown)
            {
                if (!m_isDown[index])
                {
                    m_wasPressed[index] = true;
                }
                m_isDown[index] = true;
            }
            else
            {
                if (m_isDown[index])
                {
                    m_wasReleased[index] = true;
                }
                m_isDown[index] = false;
            }
        }
    }

    bool Input::IsDown(Key key) const
    {
        return m_isDown[ToIndex(key)];
    }

    bool Input::WasPressed(Key key) const
    {
        return m_wasPressed[ToIndex(key)];
    }

    bool Input::WasReleased(Key key) const
    {
        return m_wasReleased[ToIndex(key)];
    }
}
