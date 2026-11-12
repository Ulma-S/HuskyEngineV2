// ピクセル単位の長方形(画像の中の範囲など)。プロパティにでき、保存すると {"x": .., "y": .., "width": .., "height": ..} になる。
//
// 座標は画像の左上が (0, 0)、右が +x、下が +y(画面の座標と同じ向き)。
// 大きさが 0 以下の長方形は「範囲なし」を表す(SpriteComponent::region では「画像の全体」の意味に使う)。
//
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include "core/StructReflection.h"

#include <cstdint>

namespace husky::engine
{
    struct PixelRect
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t width = 0;
        std::int32_t height = 0;

        bool IsEmpty() const { return width <= 0 || height <= 0; }
    };

    // 構造体のリフレクションの登録(StructReflection.h)。型と同じ名前空間に書く決まり。
    HUSKY_STRUCT_BEGIN(PixelRect)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
        HUSKY_PROPERTY(width)
        HUSKY_PROPERTY(height)
    HUSKY_STRUCT_END()
}
