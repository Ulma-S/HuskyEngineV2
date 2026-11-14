// マウスのボタンとイベント(エンジン独自の型。SDL の型は platform の外に出さない)。UI-1。
#pragma once

#include <cstddef> // std::size_t
#include <cstdint>

namespace husky::platform
{
    enum class MouseButton : std::uint8_t
    {
        Left,
        Right,
        Middle,

        Count // ボタンの数。配列の大きさに使う。必ず最後に置く
    };

    constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

    enum class MouseEventType : std::uint8_t
    {
        Move,       // マウスが動いた(x, y が新しい位置)
        ButtonDown, // button が押された
        ButtonUp,   // button が離された
        Wheel,      // ホイールが回った(wheel: 奥へ回すと +、手前へ回すと -)
    };

    // マウスのイベント1つ。どの種類でも、x, y に起きたときのマウスの位置を入れる。
    // 位置は窓の中の画面のピクセルで、左上が (0, 0)、右が +x、下が +y
    // (OS・SDL・ImGui・engine::ScreenToWorld の引数と同じ決まり。ワールドは上が + なので向きが逆)。
    struct MouseEvent
    {
        MouseEventType type = MouseEventType::Move;
        MouseButton button = MouseButton::Left; // ButtonDown / ButtonUp のときだけ意味がある
        float x = 0.0f;
        float y = 0.0f;
        float wheel = 0.0f; // Wheel のときだけ意味がある(1 刻みが 1)
    };
}
