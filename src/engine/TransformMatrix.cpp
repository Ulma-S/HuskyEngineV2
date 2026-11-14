// TransformMatrix.h の実装。
#include "engine/TransformMatrix.h"

#include "engine/GlmConversion.h"

#include <glm/gtc/matrix_transform.hpp> // glm::translate、glm::rotate、glm::scale、glm::radians
#include <glm/geometric.hpp>           // glm::length

#include <cmath> // std::atan2

namespace husky::engine
{
    core::Matrix4 MakeModelMatrix(const Transform& transform)
    {
        // glm::translate(m, v) は「m × 移動の行列」を返す(右から掛ける)。なので、書いた順 = 移動 × 回転 × 拡大縮小になる。
        glm::mat4 model = glm::translate(glm::mat4(1.0f), ToGlm(transform.position));
        model = glm::rotate(model, glm::radians(transform.rotation), glm::vec3(0.0f, 0.0f, 1.0f)); // 度 → ラジアン
        model = glm::scale(model, ToGlm(transform.scale));
        return FromGlm(model); // 外へは自前の型で返す
    }
}

namespace husky::engine
{
    Transform DecomposeModelMatrix(const core::Matrix4& model)
    {
        // 列優先: 列 0(m[0..3])= x の軸、列 1(m[4..7])= y の軸、列 2 = z の軸、列 3 = 位置。
        // 「移動 × 回転 × 拡大縮小」の行列では、x の軸 = (cos, sin) × scale.x、y の軸 = (-sin, cos) × scale.y。
        const float* m = model.m;
        Transform result;
        result.position = {m[12], m[13], m[14]};
        const glm::vec2 axisX(m[0], m[1]);
        const glm::vec2 axisY(m[4], m[5]);
        const float lengthX = glm::length(axisX);
        // 2つの軸で張る平行四辺形の面積(向きつき)。マイナスなら裏返っている(反転)
        const float area = axisX.x * axisY.y - axisX.y * axisY.x;
        if (lengthX > 0.0f)
        {
            result.rotation = glm::degrees(std::atan2(axisX.y, axisX.x));
            result.scale.x = lengthX;
            result.scale.y = area / lengthX; // y の軸のうち、x の軸に直角な長さ(反転ならマイナス)
        }
        else
        {
            // x の拡大縮小が 0: 向きは y の軸から決める
            const float lengthY = glm::length(axisY);
            result.rotation = lengthY > 0.0f ? glm::degrees(std::atan2(-axisY.x, axisY.y)) : 0.0f;
            result.scale.x = 0.0f;
            result.scale.y = lengthY;
        }
        result.scale.z = m[10]; // 回転は z 軸まわりだけなので、z の軸は (0, 0, scale.z) のまま
        return result;
    }
}
