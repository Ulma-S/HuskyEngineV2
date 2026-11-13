// 4×4 の行列(16 個の float)を「データとして持つ・渡す」ための型(2026-11-13 にユーザーと決定)。
// Vector3 と同じ考え方で、外部の数学ライブラリ(glm)の型を、外から見えるヘッダー(描画層やカメラの API)に出さない。
//   - 外から見えるヘッダーは、この型だけを使う(glm を別のライブラリに替えても、使う側のコードは変わらない)。
//   - 計算(掛け算、回転、逆行列など)は .cpp の中で glm に変えて行う(engine/GlmConversion.h の ToGlm / FromGlm)。
// 計算の機能は、わざと持たせない(計算は glm に任せ、自前で作り直さない)。
//
// 並べ方は「列優先」(column-major): m[列 * 4 + 行]。glm::mat4 のメモリの並びと同じで、そのまま写せる。
//   例: 移動の行列の x の移動量は 4 列目(列 3)の 1 行目(行 0)= m[12]、y は m[13]、z は m[14]。
// 動作スレッド: どこでもよい(ただの値)
#pragma once

namespace husky::core
{
    struct Matrix4
    {
        // 既定は単位行列(何もしない行列。点に掛けても変わらない)。
        float m[16] = {
            1.0f, 0.0f, 0.0f, 0.0f, // 列 0
            0.0f, 1.0f, 0.0f, 0.0f, // 列 1
            0.0f, 0.0f, 1.0f, 0.0f, // 列 2
            0.0f, 0.0f, 0.0f, 1.0f, // 列 3
        };

        // 比べるのはテストや変更の検出のため。float をそのまま比べる(誤差を許さない)。
        bool operator==(const Matrix4& other) const
        {
            for (int i = 0; i < 16; ++i)
            {
                if (m[i] != other.m[i])
                {
                    return false;
                }
            }
            return true;
        }
        bool operator!=(const Matrix4& other) const { return !(*this == other); }
    };
}
