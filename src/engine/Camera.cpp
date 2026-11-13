#include "engine/Camera.h"

#include "engine/GlmConversion.h"

#include <glm/ext/matrix_clip_space.hpp> // glm::ortho
#include <glm/ext/matrix_transform.hpp>  // glm::translate

namespace husky::engine
{
    core::Matrix4 CameraMatrices::ViewProjection() const
    {
        // 行列は右から順に掛かるので、先にビュー、次に射影が効く。計算は glm で行い、自前の型で返す。
        return FromGlm(ToGlm(projection) * ToGlm(view));
    }

    CameraMatrices MakeOrthographicCamera(float centerX, float centerY, float zoom, float screenWidth, float screenHeight)
    {
        if (zoom <= 0.0f)
        {
            zoom = 1.0f;
        }

        CameraMatrices camera;
        // ビュー行列: ワールドの位置から、カメラの位置を引く(平行移動の行列)。
        // これで「カメラから見た位置」になり、カメラのいる所が (0, 0) になる。
        camera.view = FromGlm(glm::translate(glm::mat4(1.0f), glm::vec3(-centerX, -centerY, 0.0f)));

        // 射影行列(正射影): カメラから見た位置の箱(左, 右, 下, 上, 手前, 奥)を、クリップ座標の箱(-1〜+1、z は 0〜1)に縮める。
        // 遠くのものが小さく見えることはない(2D のふつうの見え方)。奥行きは -1〜+1 の範囲を見せる(2D では z = 0 だけを使う)。
        // ズームすると、見える箱が小さくなり、同じ画面に狭い範囲を映す = 大きく見える。
        const float halfWidth = screenWidth * 0.5f / zoom;
        const float halfHeight = screenHeight * 0.5f / zoom;
        camera.projection = FromGlm(glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0f, 1.0f));
        return camera;
    }

    CameraMatrices MakeScreenCamera(float screenWidth, float screenHeight)
    {
        return MakeOrthographicCamera(screenWidth * 0.5f, screenHeight * 0.5f, 1.0f, screenWidth, screenHeight);
    }
}
