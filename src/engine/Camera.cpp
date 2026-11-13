#include "engine/Camera.h"

#include "engine/GlmConversion.h"

#include <glm/ext/matrix_clip_space.hpp> // glm::ortho
#include <glm/ext/matrix_transform.hpp>  // glm::translate
#include <glm/matrix.hpp>                // glm::inverse

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

    core::Vector3 ScreenToWorld(const CameraMatrices& camera, float screenX, float screenY, float screenWidth, float screenHeight)
    {
        if (screenWidth <= 0.0f || screenHeight <= 0.0f)
        {
            return core::Vector3{};
        }
        // 画面のピクセル → クリップ座標(左端 -1、右端 +1。y は上端 +1、下端 -1 なので裏返す)
        const float clipX = screenX / screenWidth * 2.0f - 1.0f;
        const float clipY = 1.0f - screenY / screenHeight * 2.0f;
        // 描くときは「ワールド × (射影 × ビュー)= クリップ」。逆行列を掛ければ、クリップからワールドへ戻る。
        // 正射影なので w は 1 のまま(遠近の割り算は要らない)。z は 2D の面(クリップの z = 0.5 = ワールドの z = 0)にする。
        const glm::mat4 inverse = glm::inverse(ToGlm(camera.ViewProjection()));
        const glm::vec4 world = inverse * glm::vec4(clipX, clipY, 0.5f, 1.0f);
        return core::Vector3{world.x, world.y, 0.0f};
    }

    core::Vector3 WorldToScreen(const CameraMatrices& camera, float worldX, float worldY, float screenWidth, float screenHeight)
    {
        const glm::vec4 clip = ToGlm(camera.ViewProjection()) * glm::vec4(worldX, worldY, 0.0f, 1.0f);
        // クリップ座標(左端 -1、右端 +1、上端 +1)→ 画面のピクセル(左上が 0。y は下が + なので裏返す)
        return core::Vector3{(clip.x + 1.0f) * 0.5f * screenWidth, (1.0f - clip.y) * 0.5f * screenHeight, 0.0f};
    }
}
