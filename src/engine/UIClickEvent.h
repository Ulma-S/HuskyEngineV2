// UI のボタンが押されたことを知らせるイベント(UI-6b)。Actor::OnUIClick と Component::OnUIClick に届く。
#pragma once

#include "core/Object.h" // core::ObjectHandle

namespace husky::engine
{
    struct UIClickEvent
    {
        core::ObjectHandle buttonActor; // 押されたボタンの UIActor
    };
}
