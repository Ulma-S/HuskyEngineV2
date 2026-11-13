// シーンビュー(tools/SceneView.h)のテスト。ImGui を使わない部分(ZoomAt)だけを確かめる。
#include "tools/SceneView.h"

#if HUSKY_DEVELOPMENT

#include <doctest/doctest.h>

using husky::engine::CameraView;

TEST_CASE("RS-2: ZoomAt はマウスの下のワールドの点を動かさずに、寄る・引く")
{
    const CameraView view{1000.0f, 500.0f, 1.0f};
    // 画面の (100, 100) に映っているワールドの点
    const husky::engine::CameraMatrices before = husky::engine::MakeOrthographicCamera(1000.0f, 500.0f, 1.0f, 1280.0f, 720.0f);
    const husky::core::Vector3 point = husky::engine::ScreenToWorld(before, 100.0f, 100.0f, 1280.0f, 720.0f);

    const CameraView zoomed = husky::tools::ZoomAt(view, 2.0f, 100.0f, 100.0f, 1280.0f, 720.0f);
    CHECK(zoomed.zoom == doctest::Approx(1.21f)); // 1.1 の 2 乗
    const husky::engine::CameraMatrices after =
        husky::engine::MakeOrthographicCamera(zoomed.centerX, zoomed.centerY, zoomed.zoom, 1280.0f, 720.0f);
    const husky::core::Vector3 same = husky::engine::ScreenToWorld(after, 100.0f, 100.0f, 1280.0f, 720.0f);
    CHECK(same.x == doctest::Approx(point.x));
    CHECK(same.y == doctest::Approx(point.y));

    // 引くと小さくなる。範囲の外へは行かない
    CHECK(husky::tools::ZoomAt(view, -1.0f, 640.0f, 360.0f, 1280.0f, 720.0f).zoom == doctest::Approx(1.0f / 1.1f));
    CHECK(husky::tools::ZoomAt(view, 100.0f, 640.0f, 360.0f, 1280.0f, 720.0f).zoom == 20.0f);
    CHECK(husky::tools::ZoomAt(view, -100.0f, 640.0f, 360.0f, 1280.0f, 720.0f).zoom == 0.05f);
}
#endif
