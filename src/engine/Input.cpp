// Input.h の実装。
#include "engine/Input.h"

namespace husky::engine
{
    void Input::BeginFrame(const platform::InputEvents& events)
    {
        ApplyKeyEvents(events.keys);
        ClearMouseFrameState();
        m_screenWidth = events.screenWidth;
        m_screenHeight = events.screenHeight;

        // マウスのイベントも起きた順に反映する。キーと同じく、1フレームの間に押して離しても取りこぼさない。
        for (const platform::MouseEvent& mouseEvent : events.mouse)
        {
            // どの種類のイベントにも、起きたときの位置が入っている。最後のものが、このフレームの位置になる。
            m_mousePosition = core::Vector2{mouseEvent.x, mouseEvent.y};

            const std::size_t index = ToIndex(mouseEvent.button);
            switch (mouseEvent.type)
            {
            case platform::MouseEventType::Move:
                break;
            case platform::MouseEventType::ButtonDown:
                if (!m_mouseIsDown[index])
                {
                    m_mouseWasPressed[index] = true;
                }
                m_mouseIsDown[index] = true;
                break;
            case platform::MouseEventType::ButtonUp:
                if (m_mouseIsDown[index])
                {
                    m_mouseWasReleased[index] = true;
                }
                m_mouseIsDown[index] = false;
                break;
            case platform::MouseEventType::Wheel:
                m_mouseWheel += mouseEvent.wheel; // 1フレームに何回か回れば足し合わせる
                break;
            }
        }
    }

    void Input::BeginFrame(const std::vector<platform::KeyEvent>& keyEvents)
    {
        ApplyKeyEvents(keyEvents);
        ClearMouseFrameState();
    }

    void Input::ApplyKeyEvents(const std::vector<platform::KeyEvent>& keyEvents)
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

    void Input::ClearMouseFrameState()
    {
        // 押されているか(m_mouseIsDown)と位置は、次のイベントまで続くので消さない。
        m_mouseWasPressed.fill(false);
        m_mouseWasReleased.fill(false);
        m_mouseWheel = 0.0f;
        m_mouseConsumed = false;
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

    bool Input::IsMouseDown(MouseButton button) const
    {
        return !m_mouseConsumed && m_mouseIsDown[ToIndex(button)];
    }

    bool Input::WasMousePressed(MouseButton button) const
    {
        return !m_mouseConsumed && m_mouseWasPressed[ToIndex(button)];
    }

    bool Input::WasMouseReleased(MouseButton button) const
    {
        return m_mouseWasReleased[ToIndex(button)];
    }
}
