// 2つの float(x、y)をまとめた値。スプライトのピボット(回転と拡大縮小の中心)などを「データとして持つ・保存する」ための型。
// Vector3 と同じ考え方で、外部の数学ライブラリ(glm)に依存せず、計算の機能も持たない(計算は glm で行う)。
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include "core/StructReflection.h"

namespace husky::core
{
    struct Vector2
    {
        float x = 0.0f;
        float y = 0.0f;

        // 比べるのはテストや変更の検出のため。float をそのまま比べる(誤差を許さない)。
        bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
        bool operator!=(const Vector2& other) const { return !(*this == other); }
    };

    // リフレクション対応の構造体として登録する。保存すると {"x": .., "y": ..} になる。
    HUSKY_STRUCT_BEGIN(Vector2)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
    HUSKY_STRUCT_END()
}
