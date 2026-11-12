// キーボードのキーと、キーイベントの型。
// SDL のキーの型はここに出さず、エンジン独自の型として定義する。
#pragma once

#include <cstddef> // std::size_t
#include <cstdint>

namespace husky::platform
{
    // キーの種類。文字ではなく「キーボード上の物理的な位置」を表す。
    // そのため、日本語配列でも英語配列でも、W/A/S/D は同じ場所のキーになる。
    // enum class は C# の enum に近いが、Key::Space のように必ず型名を付けて書き、int へ勝手に変換されない。
    // : std::uint8_t は「中身を 1 バイトの整数で持つ」という指定。
    enum class Key : std::uint8_t
    {
        Unknown, // 対応していないキー
        W,
        A,
        S,
        D,
        Up,
        Down,
        Left,
        Right,
        Space,
        Enter,
        Escape,
        F5, // 開発ビルドで、シーンを保存する(エディタのコマンド scene.save のショートカット)
        Z,         // 開発ビルドで、Ctrl+Z は元に戻す(editor.undo)
        Y,         // 開発ビルドで、Ctrl+Y はやり直す(editor.redo)
        LeftCtrl,  // 修飾キー。ショートカットの「Ctrl と一緒に押す」に使う
        RightCtrl,

        Count // キーの数。配列の大きさに使う。必ず最後に置く
    };

    // Key の数を整数で取る。配列の大きさや、ループの上限に使う。
    // constexpr は「コンパイル時に値が決まる」という意味で、配列の大きさに使える。
    constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);

    // キーが押された、または離されたという出来事1つ。
    struct KeyEvent
    {
        Key key = Key::Unknown;
        bool isDown = false; // true: 押された / false: 離された
    };
}
