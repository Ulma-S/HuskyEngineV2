// カメラの行列(engine/Camera.h)のテスト。ワールドの点が、クリップ座標のどこへ行くかを確かめる。
#include "engine/Camera.h"

#include <doctest/doctest.h>

#include <glm/vec4.hpp>

using husky::engine::CameraMatrices;
using husky::engine::MakeScreenCamera;

namespace
{
    // ワールドの点 (x, y, z) に行列を掛けて、クリップ座標にする。w = 1 は「位置を表す点」の印(平行移動が効く)。
    glm::vec4 ToClip(const CameraMatrices& camera, float x, float y, float z = 0.0f)
    {
        return camera.ViewProjection() * glm::vec4(x, y, z, 1.0f);
    }
}

TEST_CASE("画面のカメラ: ワールドの左下が画面の左下、右上が画面の右上になる(y は上が +)")
{
    const CameraMatrices camera = MakeScreenCamera(1280.0f, 720.0f);

    const glm::vec4 bottomLeft = ToClip(camera, 0.0f, 0.0f);
    CHECK(bottomLeft.x == doctest::Approx(-1.0f));
    CHECK(bottomLeft.y == doctest::Approx(-1.0f));
    CHECK(bottomLeft.w == doctest::Approx(1.0f)); // 正射影では w は 1 のまま(遠近で割らない)

    const glm::vec4 topRight = ToClip(camera, 1280.0f, 720.0f);
    CHECK(topRight.x == doctest::Approx(1.0f));
    CHECK(topRight.y == doctest::Approx(1.0f));

    const glm::vec4 center = ToClip(camera, 640.0f, 360.0f);
    CHECK(center.x == doctest::Approx(0.0f));
    CHECK(center.y == doctest::Approx(0.0f));
}

TEST_CASE("画面のカメラ: 1 単位 = 1 ピクセル(1 単位動くと、クリップ座標は 2 / 画面の幅だけ動く)")
{
    const CameraMatrices camera = MakeScreenCamera(1280.0f, 720.0f);
    const glm::vec4 a = ToClip(camera, 100.0f, 100.0f);
    const glm::vec4 b = ToClip(camera, 101.0f, 101.0f);
    CHECK(b.x - a.x == doctest::Approx(2.0f / 1280.0f));
    CHECK(b.y - a.y == doctest::Approx(2.0f / 720.0f)); // 上へ動くと、クリップ座標でも上(+)へ動く
}

TEST_CASE("画面のカメラ: z = 0 の奥行きは、SDL_GPU のクリップ座標の範囲(0〜1)に入る")
{
    const CameraMatrices camera = MakeScreenCamera(1280.0f, 720.0f);
    const glm::vec4 p = ToClip(camera, 10.0f, 10.0f, 0.0f);
    CHECK(p.z >= 0.0f);
    CHECK(p.z <= 1.0f);
}
