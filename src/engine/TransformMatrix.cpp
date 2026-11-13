// TransformMatrix.h の実装。
#include "engine/TransformMatrix.h"

#include "engine/GlmConversion.h"

#include <glm/gtc/matrix_transform.hpp> // glm::translate、glm::rotate、glm::scale、glm::radians

namespace husky::engine
{
    glm::mat4 MakeModelMatrix(const Transform& transform)
    {
        // glm::translate(m, v) は「m × 移動の行列」を返す(右から掛ける)。なので、書いた順 = 移動 × 回転 × 拡大縮小になる。
        glm::mat4 model = glm::translate(glm::mat4(1.0f), ToGlm(transform.position));
        model = glm::rotate(model, glm::radians(transform.rotation), glm::vec3(0.0f, 0.0f, 1.0f)); // 度 → ラジアン
        model = glm::scale(model, ToGlm(transform.scale));
        return model;
    }
}
