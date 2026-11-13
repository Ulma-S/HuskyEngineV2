// 自前のデータの型(core::Vector3 など)と、計算に使う glm の型の間の変換。
// データは自前の型で持ち、計算するときだけ glm に変える(core/Vector3.h を参照)。
// glm を知っているのはこのヘッダの側だけで、core::Vector3 は glm を知らない(core は外部の数学ライブラリに依存しない)。
// 動作スレッド: どこでもよい(ただの計算)
#pragma once

#include "core/Matrix4.h"
#include "core/Vector3.h"

#include <glm/gtc/type_ptr.hpp> // glm::make_mat4(float の並びから行列を作る)、glm::value_ptr(行列の中の float の並び)
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm> // std::copy

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

    // 行列: どちらも列優先で同じ並びなので、16 個の float をそのまま写す。
    inline glm::mat4 ToGlm(const core::Matrix4& value)
    {
        return glm::make_mat4(value.m);
    }

    inline core::Matrix4 FromGlm(const glm::mat4& value)
    {
        core::Matrix4 result;
        const float* source = glm::value_ptr(value);
        std::copy(source, source + 16, result.m);
        return result;
    }
}
