// Input のテスト。キーボードの代わりに、偽のキーイベントを渡して入力を再現する。
#include "engine/Input.h"

#include <doctest/doctest.h>

#include <utility> // std::move
#include <vector>

using husky::engine::Input;
using husky::platform::Key;
using husky::platform::KeyEvent;

namespace
{
    KeyEvent Down(Key key) { return KeyEvent{key, true}; }
    KeyEvent Up(Key key) { return KeyEvent{key, false}; }

    // イベントがない1フレーム分。
    const std::vector<KeyEvent> NoEvents;
}

TEST_CASE("Input: 何も押していなければ、すべて false")
{
    Input input;
    input.BeginFrame(NoEvents);

    CHECK_FALSE(input.IsDown(Key::Space)); // CHECK_FALSE は「この条件が成り立たないはず」
    CHECK_FALSE(input.WasPressed(Key::Space));
    CHECK_FALSE(input.WasReleased(Key::Space));
}

TEST_CASE("Input: 押した瞬間のフレームだけ WasPressed が true になる")
{
    Input input;

    input.BeginFrame({Down(Key::Space)}); // {} で vector をその場で作って渡せる
    CHECK(input.IsDown(Key::Space));
    CHECK(input.WasPressed(Key::Space));
    CHECK_FALSE(input.WasReleased(Key::Space));

    // 次のフレーム: 押しっぱなし。押されてはいるが、「押した瞬間」ではない。
    input.BeginFrame(NoEvents);
    CHECK(input.IsDown(Key::Space));
    CHECK_FALSE(input.WasPressed(Key::Space));
}

TEST_CASE("Input: 離した瞬間のフレームだけ WasReleased が true になる")
{
    Input input;
    input.BeginFrame({Down(Key::Space)});

    input.BeginFrame({Up(Key::Space)});
    CHECK_FALSE(input.IsDown(Key::Space));
    CHECK(input.WasReleased(Key::Space));

    input.BeginFrame(NoEvents);
    CHECK_FALSE(input.WasReleased(Key::Space));
}

TEST_CASE("Input: 1フレームの間に押して離しても、取りこぼさない")
{
    Input input;
    input.BeginFrame({Down(Key::Space), Up(Key::Space)});

    CHECK(input.WasPressed(Key::Space));
    CHECK(input.WasReleased(Key::Space));
    CHECK_FALSE(input.IsDown(Key::Space)); // 最後は離されている
}

TEST_CASE("Input: キーごとに独立している")
{
    Input input;
    input.BeginFrame({Down(Key::W), Down(Key::Left)});

    CHECK(input.IsDown(Key::W));
    CHECK(input.IsDown(Key::Left));
    CHECK_FALSE(input.IsDown(Key::Space));
}

// ---- マウス(UI-1) ----

namespace
{
    using husky::platform::InputEvents;
    using husky::platform::MouseButton;
    using husky::platform::MouseEvent;
    using husky::platform::MouseEventType;

    MouseEvent MouseAt(MouseEventType type, float x, float y, MouseButton button = MouseButton::Left, float wheel = 0.0f)
    {
        MouseEvent event;
        event.type = type;
        event.button = button;
        event.x = x;
        event.y = y;
        event.wheel = wheel;
        return event;
    }

    InputEvents MouseEvents(std::vector<MouseEvent> mouse)
    {
        InputEvents events;
        events.mouse = std::move(mouse);
        return events;
    }
}

TEST_CASE("Input: マウスのボタンも、押した・離した瞬間のフレームだけ true になり、押している間は IsMouseDown")
{
    Input input;
    input.BeginFrame(MouseEvents({MouseAt(MouseEventType::ButtonDown, 10.0f, 20.0f)}));
    CHECK(input.IsMouseDown(MouseButton::Left));
    CHECK(input.WasMousePressed(MouseButton::Left));
    CHECK_FALSE(input.IsMouseDown(MouseButton::Right)); // ボタンごとに別

    input.BeginFrame(InputEvents{}); // 押しっぱなし
    CHECK(input.IsMouseDown(MouseButton::Left));
    CHECK_FALSE(input.WasMousePressed(MouseButton::Left));

    input.BeginFrame(MouseEvents({MouseAt(MouseEventType::ButtonUp, 10.0f, 20.0f)}));
    CHECK_FALSE(input.IsMouseDown(MouseButton::Left));
    CHECK(input.WasMouseReleased(MouseButton::Left));

    input.BeginFrame(InputEvents{});
    CHECK_FALSE(input.WasMouseReleased(MouseButton::Left));
}

TEST_CASE("Input: 1フレームの間にマウスのボタンを押して離しても、取りこぼさない")
{
    Input input;
    input.BeginFrame(MouseEvents({MouseAt(MouseEventType::ButtonDown, 0.0f, 0.0f, MouseButton::Right),
                                  MouseAt(MouseEventType::ButtonUp, 0.0f, 0.0f, MouseButton::Right)}));
    CHECK(input.WasMousePressed(MouseButton::Right));
    CHECK(input.WasMouseReleased(MouseButton::Right));
    CHECK_FALSE(input.IsMouseDown(MouseButton::Right));
}

TEST_CASE("Input: マウスの位置は最後のイベントの位置で、イベントの無いフレームも続く。ホイールはそのフレームの合計")
{
    Input input;
    InputEvents events = MouseEvents({MouseAt(MouseEventType::Move, 5.0f, 6.0f),
                                      MouseAt(MouseEventType::Wheel, 7.0f, 8.0f, MouseButton::Left, 1.0f),
                                      MouseAt(MouseEventType::Wheel, 9.0f, 10.0f, MouseButton::Left, 2.0f)});
    events.screenWidth = 640.0f;
    events.screenHeight = 480.0f;
    input.BeginFrame(events);
    CHECK(input.GetMousePosition().x == 9.0f);
    CHECK(input.GetMousePosition().y == 10.0f);
    CHECK(input.GetMouseWheel() == 3.0f);
    CHECK(input.GetScreenWidth() == 640.0f);
    CHECK(input.GetScreenHeight() == 480.0f);

    input.BeginFrame(InputEvents{});
    CHECK(input.GetMousePosition().x == 9.0f); // 動かなければ前の位置のまま
    CHECK(input.GetMouseWheel() == 0.0f);      // ホイールは1フレームだけ
}

TEST_CASE("Input: マウスを UI が使ったフレームは、押した・押している・ホイールがゲームから見えない")
{
    Input input;
    input.BeginFrame(MouseEvents({MouseAt(MouseEventType::ButtonDown, 0.0f, 0.0f),
                                  MouseAt(MouseEventType::Wheel, 0.0f, 0.0f, MouseButton::Left, 1.0f)}));
    input.ConsumeMouse();
    CHECK(input.IsMouseConsumed());
    CHECK_FALSE(input.WasMousePressed(MouseButton::Left));
    CHECK_FALSE(input.IsMouseDown(MouseButton::Left));
    CHECK(input.GetMouseWheel() == 0.0f);

    // 印は次のフレームで外れる(押している状態はそのまま残っている)
    input.BeginFrame(InputEvents{});
    CHECK_FALSE(input.IsMouseConsumed());
    CHECK(input.IsMouseDown(MouseButton::Left));
}
