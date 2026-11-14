// シーンビュー(tools/SceneView.h)のテスト。ImGui を使わない部分(ZoomAt)だけを確かめる。
#include "tools/SceneView.h"
#include "tools/SceneGrid.h"
#include "tools/TransformGizmo.h"
#include "tools/EditorCommands.h"
#include "engine/Actor.h"
#include "engine/Application.h"

#if HUSKY_DEVELOPMENT

#include <doctest/doctest.h>

#include <cmath>  // std::fabs
#include <string>

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

TEST_CASE("RS-3: CommitActorTransform はドラッグで変えた transform を property.set で書き、Undo でつかむ前へ戻る")
{
    husky::engine::Application app;
    husky::tools::EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));
    husky::engine::Actor* actor = app.GetWorld().Spawn<husky::engine::Actor>();
    actor->transform.position = {10.0f, 20.0f, 5.0f};

    // ドラッグ中はその場で書き換わっている(履歴にはまだ無い)
    const husky::engine::Transform fromTransform = actor->transform;
    actor->transform.position.x = 50.0f;
    actor->transform.position.y = 60.0f;
    REQUIRE(husky::tools::CommitActorTransform(app, commands, *actor, fromTransform));
    CHECK(actor->transform.position.x == 50.0f);
    CHECK(actor->transform.position.y == 60.0f);
    CHECK(actor->transform.position.z == 5.0f); // z は変えない

    REQUIRE(commands.Execute("editor.undo", app).succeeded); // 1回でつかむ前の位置へ
    CHECK(actor->transform.position.x == 10.0f);
    CHECK(actor->transform.position.y == 20.0f);
    REQUIRE(commands.Execute("editor.redo", app).succeeded);
    CHECK(actor->transform.position.x == 50.0f);
    CHECK(actor->transform.position.y == 60.0f);

    // property.set が無い(使えない)ときは、動かした所のまま false
    husky::tools::EditorCommandRegistry none;
    actor->transform.position.x = 70.0f;
    CHECK_FALSE(husky::tools::CommitActorTransform(app, none, *actor, fromTransform));
    CHECK(actor->transform.position.x == 70.0f);
}

TEST_CASE("RS-4: ギズモの部品の当たり(画面のピクセル。真ん中の四角を先に調べる)")
{
    using husky::tools::GizmoHandle;
    using husky::tools::PickGizmoHandle;
    // ギズモの真ん中が画面の (500, 300)
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 503.0f, 297.0f) == GizmoHandle::Center);
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 560.0f, 302.0f) == GizmoHandle::X);  // 右へ 60(軸の上)
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 498.0f, 240.0f) == GizmoHandle::Y);  // 上へ 60(画面の y は小さくなる)
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 500.0f, 360.0f) == GizmoHandle::None); // 下には矢印が無い
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 440.0f, 300.0f) == GizmoHandle::None); // 左にも無い
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 560.0f, 320.0f) == GizmoHandle::None); // 軸から離れすぎ
    CHECK(PickGizmoHandle(husky::tools::GizmoTool::Move, 500.0f, 300.0f, 588.0f, 300.0f) == GizmoHandle::X);  // 先の菱形まで
}

TEST_CASE("RS-4: ギズモのドラッグは、つかんだ部品の軸だけに動いた量を足す")
{
    using husky::tools::ApplyGizmoDrag;
    using husky::tools::GizmoHandle;
    const husky::core::Vector3 from{10.0f, 20.0f, 3.0f};
    const husky::core::Vector3 x = ApplyGizmoDrag(GizmoHandle::X, from, 5.0f, 7.0f);
    CHECK(x.x == 15.0f);
    CHECK(x.y == 20.0f);
    const husky::core::Vector3 y = ApplyGizmoDrag(GizmoHandle::Y, from, 5.0f, 7.0f);
    CHECK(y.x == 10.0f);
    CHECK(y.y == 27.0f);
    const husky::core::Vector3 both = ApplyGizmoDrag(GizmoHandle::Center, from, 5.0f, 7.0f);
    CHECK(both.x == 15.0f);
    CHECK(both.y == 27.0f);
    CHECK(both.z == 3.0f);
}

TEST_CASE("RS-4: ギズモの線は画面で一定の大きさ(ズームすると、ワールドでの大きさが逆に変わる)。乗せた部品は光る")
{
    using husky::tools::GizmoHandle;
    std::vector<husky::render::Sprite> quads;
    husky::tools::BuildGizmoSprites(husky::tools::GizmoTool::Move, CameraView{0.0f, 0.0f, 1.0f}, husky::core::Vector3{100.0f, 50.0f, 0.0f}, 0.0f,
                                    GizmoHandle::None, quads);
    REQUIRE(quads.size() == 5); // 真ん中、x の軸と先、y の軸と先
    CHECK(quads[0].model.m[12] == doctest::Approx(100.0f)); // 真ん中の四角はアクターの位置
    CHECK(quads[0].width == doctest::Approx(husky::tools::GizmoSize::CenterSize));
    CHECK(quads[1].width == doctest::Approx(husky::tools::GizmoSize::ArrowLength)); // x の軸
    const float normalAlpha = quads[1].color.g;

    // 2 倍に寄ると、ワールドでは半分の大きさ(画面では同じ)。x の矢印を光らせる
    husky::tools::BuildGizmoSprites(husky::tools::GizmoTool::Move, CameraView{0.0f, 0.0f, 2.0f}, husky::core::Vector3{100.0f, 50.0f, 0.0f}, 0.0f,
                                    GizmoHandle::X, quads);
    REQUIRE(quads.size() == 5);
    CHECK(quads[1].width == doctest::Approx(husky::tools::GizmoSize::ArrowLength / 2.0f));
    CHECK(quads[1].model.m[12] == doctest::Approx(100.0f + husky::tools::GizmoSize::ArrowLength / 4.0f));
    CHECK(quads[1].color.g != normalAlpha); // 色が変わった(光った)
    CHECK(quads[3].color.g == doctest::Approx(0.85f)); // y の軸はふつうの緑のまま
}

TEST_CASE("スナップ: snap を渡すと、動かした軸だけを snap の倍数(グリッドの線の上)に丸める")
{
    using husky::tools::ApplyGizmoDrag;
    using husky::tools::GizmoHandle;
    const husky::core::Vector3 from{2019.9998f, 1083.0f, 3.0f};
    // x の矢印: x は 10 の倍数へ。y は動かしていないので、半端な値のまま
    const husky::core::Vector3 x = ApplyGizmoDrag(GizmoHandle::X, from, 4.0f, 9.0f, 10.0f);
    CHECK(x.x == 2020.0f);
    CHECK(x.y == 1083.0f);
    // 真ん中: 両方を丸める(23 → 20、27 → 30。負の側も同じ)
    const husky::core::Vector3 both = ApplyGizmoDrag(GizmoHandle::Center, husky::core::Vector3{0.0f, 0.0f, 0.0f}, 23.0f, -27.0f, 10.0f);
    CHECK(both.x == 20.0f);
    CHECK(both.y == -30.0f);
    // snap が 0 なら丸めない(Ctrl を押していないとき)
    CHECK(ApplyGizmoDrag(GizmoHandle::X, from, 4.0f, 0.0f, 0.0f).x == doctest::Approx(2023.9998f));
}

TEST_CASE("RS-5: 回転のギズモは輪だけをつかめ、マウスが真ん中のまわりに回った角度(反時計回りが +)を足す")
{
    using husky::tools::GizmoHandle;
    using husky::tools::GizmoTool;
    using husky::tools::PickGizmoHandle;
    // 真ん中 (500, 300)、半径 70 の輪
    CHECK(PickGizmoHandle(GizmoTool::Rotate, 500.0f, 300.0f, 570.0f, 300.0f) == GizmoHandle::Ring);
    CHECK(PickGizmoHandle(GizmoTool::Rotate, 500.0f, 300.0f, 500.0f, 232.0f) == GizmoHandle::Ring); // 上(少し内側)
    CHECK(PickGizmoHandle(GizmoTool::Rotate, 500.0f, 300.0f, 500.0f, 300.0f) == GizmoHandle::None); // 真ん中は輪ではない
    CHECK(PickGizmoHandle(GizmoTool::Rotate, 500.0f, 300.0f, 600.0f, 300.0f) == GizmoHandle::None); // 外すぎ

    // 右(0 度)から上(画面では y が小さい)へ: 反時計回りに 90 度
    CHECK(husky::tools::MouseAngleDelta(500.0f, 300.0f, 570.0f, 300.0f, 500.0f, 230.0f) == doctest::Approx(90.0f));
    // 右から下へ: 時計回りに 90 度 = -90
    CHECK(husky::tools::MouseAngleDelta(500.0f, 300.0f, 570.0f, 300.0f, 500.0f, 370.0f) == doctest::Approx(-90.0f));
    // 左の境目(±180 度)をまたぐ小さな動きは、小さな角度のまま(359 度にならない)
    const float across = husky::tools::MouseAngleDelta(500.0f, 300.0f, 430.0f, 301.0f, 430.0f, 299.0f);
    CHECK(std::fabs(across) < 5.0f);

    // 足した角度。Ctrl のスナップは 15 度ずつ
    CHECK(husky::tools::ApplyRotateDrag(10.0f, 22.0f) == doctest::Approx(32.0f));
    CHECK(husky::tools::ApplyRotateDrag(10.0f, 22.0f, 15.0f) == doctest::Approx(30.0f));
    CHECK(husky::tools::ApplyRotateDrag(0.0f, 400.0f) == doctest::Approx(400.0f)); // 1 周を越えても続けて足す
}

TEST_CASE("RS-5: 拡大縮小のギズモは軸の長さだけ引っ張ると 2 倍。軸はアクターと一緒に回る")
{
    using husky::tools::ApplyScaleDrag;
    using husky::tools::GizmoHandle;
    using husky::tools::GizmoTool;
    const float length = husky::tools::GizmoSize::ArrowLength;
    const husky::core::Vector3 from{1.0f, 2.0f, 1.0f};
    // 回っていない: x の軸を右へ 80 ピクセル → scale.x は 2 倍。y はそのまま
    const husky::core::Vector3 x = ApplyScaleDrag(GizmoHandle::X, from, 0.0f, 500.0f, 300.0f, 500.0f + length, 300.0f);
    CHECK(x.x == doctest::Approx(2.0f));
    CHECK(x.y == doctest::Approx(2.0f));
    // y の軸を上へ 40 ピクセル(画面の y は小さくなる)→ scale.y は 1.5 倍
    const husky::core::Vector3 y = ApplyScaleDrag(GizmoHandle::Y, from, 0.0f, 500.0f, 300.0f, 500.0f, 300.0f - length * 0.5f);
    CHECK(y.y == doctest::Approx(3.0f));
    CHECK(y.x == doctest::Approx(1.0f));
    // 真ん中: 右へ 80 → 両方 1.5 倍(右と上の平均)。Ctrl のスナップは 0.1 ずつ
    const husky::core::Vector3 both = ApplyScaleDrag(GizmoHandle::Center, from, 0.0f, 500.0f, 300.0f, 500.0f + length, 300.0f);
    CHECK(both.x == doctest::Approx(1.5f));
    CHECK(both.y == doctest::Approx(3.0f));
    const husky::core::Vector3 snapped =
        ApplyScaleDrag(GizmoHandle::X, from, 0.0f, 500.0f, 300.0f, 500.0f + 13.0f, 300.0f, 0.1f); // 1.1625 → 1.2
    CHECK(snapped.x == doctest::Approx(1.2f));

    // 90 度回ったアクター: ローカルの x の軸は画面の上を向く。上へ 80 引っ張ると scale.x が 2 倍
    const husky::core::Vector3 rotated = ApplyScaleDrag(GizmoHandle::X, from, 90.0f, 500.0f, 300.0f, 500.0f, 300.0f - length);
    CHECK(rotated.x == doctest::Approx(2.0f));
    CHECK(rotated.y == doctest::Approx(2.0f));
    // 当たりも回る: 90 度回ったアクターの x の軸は、真ん中の上にある
    CHECK(husky::tools::PickGizmoHandle(GizmoTool::Scale, 500.0f, 300.0f, 500.0f, 240.0f, 90.0f) == GizmoHandle::X);
    CHECK(husky::tools::PickGizmoHandle(GizmoTool::Scale, 500.0f, 300.0f, 560.0f, 300.0f, 90.0f) == GizmoHandle::None);
}

TEST_CASE("RS-5: 道具ごとのギズモの形(回転は輪と向きの線、拡大縮小は回った軸)")
{
    using husky::tools::GizmoHandle;
    using husky::tools::GizmoTool;
    std::vector<husky::render::Sprite> quads;
    husky::tools::BuildGizmoSprites(GizmoTool::Rotate, CameraView{0.0f, 0.0f, 1.0f}, husky::core::Vector3{0.0f, 0.0f, 0.0f},
                                    0.0f, GizmoHandle::None, quads);
    CHECK(quads.size() == static_cast<std::size_t>(husky::tools::GizmoSize::RingSegments + 1));
    // 拡大縮小: 90 度回ったアクターの x の軸の線は、真ん中の上(x は 0、y は 40)
    husky::tools::BuildGizmoSprites(GizmoTool::Scale, CameraView{0.0f, 0.0f, 1.0f}, husky::core::Vector3{0.0f, 0.0f, 0.0f},
                                    90.0f, GizmoHandle::None, quads);
    REQUIRE(quads.size() == 5);
    CHECK(quads[1].model.m[12] == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(quads[1].model.m[13] == doctest::Approx(40.0f));
    CHECK(std::string(husky::tools::GetGizmoToolName(GizmoTool::Scale)) == "Scale (R)");
}

TEST_CASE("RS-5: CommitActorTransform は位置・回転・拡大縮小をまとめて1件の Undo にする")
{
    husky::engine::Application app;
    husky::tools::EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    husky::engine::Actor* actor = app.GetWorld().Spawn<husky::engine::Actor>();
    const husky::engine::Transform before = actor->transform;
    actor->transform.rotation = 45.0f;
    actor->transform.scale = {2.0f, 3.0f, 1.0f};
    REQUIRE(husky::tools::CommitActorTransform(app, commands, *actor, before));
    CHECK(actor->transform.rotation == 45.0f);
    CHECK(actor->transform.scale.y == 3.0f);
    REQUIRE(commands.Execute("editor.undo", app).succeeded);
    CHECK(actor->transform.rotation == 0.0f);
    CHECK(actor->transform.scale.x == 1.0f);
    CHECK(actor->transform.scale.y == 1.0f);
}

TEST_CASE("Global / Local: Local の移動の矢印はアクターと一緒に回り、動きはアクターの向きの軸に沿う")
{
    using husky::tools::ApplyGizmoDrag;
    using husky::tools::GizmoHandle;
    using husky::tools::GizmoSpace;
    using husky::tools::GizmoTool;
    using husky::tools::PickGizmoHandle;
    // 90 度回ったアクター: Local の x の矢印は上を向く。Global なら右のまま
    CHECK(PickGizmoHandle(GizmoTool::Move, 500.0f, 300.0f, 500.0f, 240.0f, 90.0f, GizmoSpace::Local) == GizmoHandle::X);
    CHECK(PickGizmoHandle(GizmoTool::Move, 500.0f, 300.0f, 500.0f, 240.0f, 90.0f, GizmoSpace::World) == GizmoHandle::Y);
    CHECK(PickGizmoHandle(GizmoTool::Move, 500.0f, 300.0f, 560.0f, 300.0f, 90.0f, GizmoSpace::World) == GizmoHandle::X);

    // Local の x の矢印をつかんで、右へ 30・上へ 50 動かす: アクターの x の向き(ワールドの上)の分だけ進む = y が +50
    const husky::core::Vector3 from{10.0f, 20.0f, 0.0f};
    const husky::core::Vector3 local = ApplyGizmoDrag(GizmoHandle::X, from, 30.0f, 50.0f, 0.0f, 90.0f, GizmoSpace::Local);
    CHECK(local.x == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(local.y == doctest::Approx(70.0f));
    // 同じ動きを Global の x で: x が +30
    const husky::core::Vector3 global = ApplyGizmoDrag(GizmoHandle::X, from, 30.0f, 50.0f, 0.0f, 90.0f, GizmoSpace::World);
    CHECK(global.x == doctest::Approx(40.0f));
    CHECK(global.y == doctest::Approx(20.0f));
    // Local のスナップは、軸に沿って進んだ距離を丸める(54 → 50。位置そのものではない)
    const husky::core::Vector3 snapped = ApplyGizmoDrag(GizmoHandle::X, from, 0.0f, 54.0f, 10.0f, 90.0f, GizmoSpace::Local);
    CHECK(snapped.y == doctest::Approx(70.0f));
    // 45 度回ったアクター: Local の x の矢印に沿って √2 * 10 だけ動かすと、x・y が 10 ずつ進む
    const husky::core::Vector3 diagonal = ApplyGizmoDrag(GizmoHandle::X, from, 10.0f, 10.0f, 0.0f, 45.0f, GizmoSpace::Local);
    CHECK(diagonal.x == doctest::Approx(20.0f));
    CHECK(diagonal.y == doctest::Approx(30.0f));
}

TEST_CASE("Global / Local: Local のときだけ移動の矢印を回して描く。名前は Unity と同じ")
{
    using husky::tools::GizmoHandle;
    using husky::tools::GizmoSpace;
    using husky::tools::GizmoTool;
    std::vector<husky::render::Sprite> quads;
    husky::tools::BuildGizmoSprites(GizmoTool::Move, CameraView{0.0f, 0.0f, 1.0f}, husky::core::Vector3{0.0f, 0.0f, 0.0f},
                                    90.0f, GizmoHandle::None, quads, GizmoSpace::Local);
    REQUIRE(quads.size() == 5);
    CHECK(quads[1].model.m[13] == doctest::Approx(40.0f)); // x の矢印の線は上
    husky::tools::BuildGizmoSprites(GizmoTool::Move, CameraView{0.0f, 0.0f, 1.0f}, husky::core::Vector3{0.0f, 0.0f, 0.0f},
                                    90.0f, GizmoHandle::None, quads, GizmoSpace::World);
    CHECK(quads[1].model.m[12] == doctest::Approx(40.0f)); // Global では右のまま
    CHECK(std::string(husky::tools::GetGizmoSpaceName(GizmoSpace::Local)) == "Local (X)");
    CHECK(std::string(husky::tools::GetGizmoSpaceName(GizmoSpace::World)) == "Global (X)");
}

TEST_CASE("仕上げ: view.grid は呼ぶたびにグリッドを出す・隠すを切り替える")
{
    husky::engine::Application app;
    husky::tools::EditorCommandRegistry commands;
    husky::tools::SceneView sceneView;
    sceneView.RegisterCommands(commands);
    CHECK(sceneView.IsGridShown());
    REQUIRE(commands.Execute("view.grid", app).succeeded);
    CHECK_FALSE(sceneView.IsGridShown());
    REQUIRE(commands.Execute("view.grid", app).succeeded);
    CHECK(sceneView.IsGridShown());
    const husky::tools::EditorCommandResult hidden = commands.Execute("view.grid", app);
    REQUIRE(hidden.succeeded);
    CHECK(hidden.message == "Grid: hidden");
    CHECK_FALSE(sceneView.IsGridShown());
    CHECK(commands.Execute("editor.undo", app).succeeded == false); // 見た目だけなので、履歴には積まない(undo も無い)
}

TEST_CASE("仕上げ: 選んだアクターの枠は、スプライトの四隅を結ぶ 4 本の線(回っていれば枠も回る)")
{
    // 真ん中 (0, 0)、40 x 20 のスプライトの四隅
    husky::render::Sprite shape;
    shape.width = 40.0f;
    shape.height = 20.0f;
    std::vector<husky::render::Sprite> lines;
    husky::tools::BuildSelectionOutline(husky::render::ComputeSpriteCorners(shape), CameraView{0.0f, 0.0f, 1.0f}, lines);
    REQUIRE(lines.size() == 4);
    // 1 本目は上の辺(左上 → 右上): 真ん中は (0, 10)、長さ 40(+ 太さの分)、向き 0 度
    CHECK(lines[0].model.m[12] == doctest::Approx(0.0f));
    CHECK(lines[0].model.m[13] == doctest::Approx(10.0f));
    CHECK(lines[0].width == doctest::Approx(40.0f + husky::tools::GizmoSize::Thickness));
    // 2 本目は右の辺(右上 → 右下): 真ん中は (20, 0)
    CHECK(lines[1].model.m[12] == doctest::Approx(20.0f));
    CHECK(lines[1].model.m[13] == doctest::Approx(0.0f).epsilon(0.001));
    // 2 倍に寄ると、線の太さはワールドで半分(画面では同じ)
    husky::tools::BuildSelectionOutline(husky::render::ComputeSpriteCorners(shape), CameraView{0.0f, 0.0f, 2.0f}, lines);
    CHECK(lines[0].height == doctest::Approx(husky::tools::GizmoSize::Thickness / 2.0f));
}
#endif
