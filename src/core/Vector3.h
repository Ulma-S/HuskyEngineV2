// 3つの float(x、y、z)をまとめた値。位置、拡大縮小などを「データとして持つ・保存する」ための型。
// 外部の数学ライブラリ(glm)に依存しない(2026-11-13 にユーザーと決定)。
//   - データの型(アクターが持つ、保存する、インスペクタで見せる)は、この自前の型にする。
//   - 計算(行列、回転など)は glm で行い、境目で変換する(engine/GlmConversion.h の ToGlm / FromGlm)。
// こうすると、ゲームのデータとファイルの形が、ライブラリの型の都合(中の union など)に縛られない。
// 計算の機能(足し算、長さなど)は、わざと持たせない(計算は glm に任せ、自前で作り直さない)。
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include "core/StructReflection.h"

namespace husky::core
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        // 比べるのはテストや変更の検出のため。float をそのまま比べる(誤差を許さない)。
        bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z; }
        bool operator!=(const Vector3& other) const { return !(*this == other); }
    };

    // リフレクション対応の構造体として登録する。保存すると {"x": .., "y": .., "z": ..} になる。
    HUSKY_STRUCT_BEGIN(Vector3)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
        HUSKY_PROPERTY(z)
    HUSKY_STRUCT_END()
}
