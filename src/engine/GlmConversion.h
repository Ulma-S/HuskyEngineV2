// 自前のデータの型(core::Vector3 など)と、計算に使う glm の型の間の変換。
// データは自前の型で持ち、計算するときだけ glm に変える(core/Vector3.h を参照)。
// glm を知っているのはこのヘッダの側だけで、core::Vector3 は glm を知らない(core は外部の数学ライブラリに依存しない)。
// 動作スレッド: どこでもよい(ただの計算)
#pragma once

#include "core/Vector3.h"

#include <glm/vec3.hpp>

namespace husky::engine
{
    inline glm::vec3 ToGlm(const core::Vector3& value)
    {
        return glm::vec3(value.x, value.y, value.z);
    }

    inline core::Vector3 FromGlm(const glm::vec3& value)
    {
        return core::Vector3{value.x, value.y, value.z};
    }
}
