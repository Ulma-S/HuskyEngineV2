// Input のテスト。キーボードの代わりに、偽のキーイベントを渡して入力を再現する。
#include "engine/Input.h"

#include <doctest/doctest.h>

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
