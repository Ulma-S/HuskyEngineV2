// シーンビュー(tools/SceneView.h)のテスト。ImGui を使わない部分(ZoomAt)だけを確かめる。
#include "tools/SceneView.h"
#include "tools/SceneGrid.h"

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

TEST_CASE("グリッド: 線の間隔は、画面で 16 ピクセル以上離れる、いちばん細かい 10 のべき乗")
{
    CHECK(husky::tools::ChooseGridSpacing(1.0f) == 100.0f);   // 10 だと 10 ピクセルで細かすぎる
    CHECK(husky::tools::ChooseGridSpacing(2.0f) == 10.0f);    // 10 x 2 = 20 ピクセル
    CHECK(husky::tools::ChooseGridSpacing(20.0f) == 1.0f);
    CHECK(husky::tools::ChooseGridSpacing(0.1f) == 1000.0f);  // 引くと粗くなる
}

TEST_CASE("グリッド: 見えている範囲の線を作り、原点を通る線は軸の色、10 本ごとは濃い線")
{
    // 真ん中 (0, 0)、zoom 1、画面 1280 x 720 → 見える範囲は x -640〜640、y -360〜360。間隔は 100
    std::vector<husky::render::Sprite> lines;
    husky::tools::BuildGridSprites(CameraView{0.0f, 0.0f, 1.0f}, 1280.0f, 720.0f, lines);
    // 縦: -600〜600 の 13 本、横: -300〜300 の 7 本
    REQUIRE(lines.size() == 20);
    int axes = 0;
    for (const husky::render::Sprite& line : lines)
    {
        const bool vertical = line.width < line.height;
        const float at = vertical ? line.model.m[12] : line.model.m[13];
        if (at == 0.0f)
        {
            ++axes;
            CHECK(line.color.a == doctest::Approx(0.6f)); // 軸
        }
        CHECK(line.texture.IsNull()); // 白い板
    }
    CHECK(axes == 2);
    // 線の太さは画面で 1 ピクセル(zoom 1 なら 1 単位)。長さは見えている範囲いっぱい
    CHECK(lines[0].width == doctest::Approx(1.0f));
    CHECK(lines[0].height == doctest::Approx(720.0f));

    // 作り直すと前の中身は消える。寄ると線の間隔が細かくなり、10 本ごとの線(x = 100)は濃い
    husky::tools::BuildGridSprites(CameraView{50.0f, 0.0f, 2.0f}, 1280.0f, 720.0f, lines); // 間隔 10、x は -270〜370
    bool foundMajor = false;
    for (const husky::render::Sprite& line : lines)
    {
        if (line.width < line.height && line.model.m[12] == 100.0f)
        {
            foundMajor = true;
            CHECK(line.color.a == doctest::Approx(0.2f));
            CHECK(line.width == doctest::Approx(0.5f)); // 1 / zoom
        }
    }
    CHECK(foundMajor);
    CHECK(lines.size() == 65 + 37); // 縦: x が -270〜370 の 65 本、横: y が -180〜180 の 37 本
}
#endif
