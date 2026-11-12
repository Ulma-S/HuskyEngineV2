#include "engine/Camera.h"

#include <glm/ext/matrix_clip_space.hpp> // glm::ortho

namespace husky::engine
{
    CameraMatrices MakeScreenCamera(float screenWidth, float screenHeight)
    {
        CameraMatrices camera;
        // 正射影: 見える範囲の箱(左, 右, 下, 上, 手前, 奥)を、クリップ座標の箱(-1〜+1、z は 0〜1)に縮める行列。
        // 遠くのものが小さく見えることはない(2D のふつうの見え方)。
        // 奥行きは -1〜+1 の範囲を見せる(2D では z = 0 だけを使う)。
        camera.projection = glm::ortho(0.0f, screenWidth, 0.0f, screenHeight, -1.0f, 1.0f);
        // camera.view は単位行列のまま(カメラは原点から動かない)。
        return camera;
    }
}
