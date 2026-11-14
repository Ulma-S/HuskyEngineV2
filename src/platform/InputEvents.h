// 1フレームの間に OS から届いた入力のイベントの入れ物(キーとマウス)。UI-1。
// platform::ProcessEvents が埋め、engine::Input::BeginFrame が読む。テストは自分で埋めて渡す。
#pragma once

#include "platform/Keyboard.h"
#include "platform/Mouse.h"

#include <vector>

namespace husky::platform
{
    struct InputEvents
    {
        std::vector<KeyEvent> keys;    // 起きた順
        std::vector<MouseEvent> mouse; // 起きた順

        // 画面(窓の中の描く所)の大きさ(ピクセル)。マウスの位置をワールドへ直すのに使う。
        // OS のイベントではないが、毎フレームの入力と一緒に渡す(テストで固定の値を入れられるように)。
        // 0 のときは「画面が無い」(テストや CLI)。
        float screenWidth = 0.0f;
        float screenHeight = 0.0f;

        // 中身を空にする。clear() は確保したメモリを残すので、毎フレーム使い回しても確保は起きない。
        void Clear()
        {
            keys.clear();
            mouse.clear();
        }
    };
}
