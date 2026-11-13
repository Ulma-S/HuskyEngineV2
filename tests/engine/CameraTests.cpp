// カメラの行列(engine/Camera.h)のテスト。ワールドの点が、クリップ座標のどこへ行くかを確かめる。
#include "engine/Camera.h"

#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/CameraComponent.h"
#include "engine/GlmConversion.h"
#include "engine/Scene.h"

#include <doctest/doctest.h>

#include <glm/vec4.hpp>

using husky::engine::CameraMatrices;
using husky::engine::MakeScreenCamera;

namespace
{
    // ワールドの点 (x, y, z) に行列を掛けて、クリップ座標にする。w = 1 は「位置を表す点」の印(平行移動が効く)。
    glm::vec4 ToClip(const CameraMatrices& camera, float x, float y, float z = 0.0f)
    {
        return husky::engine::ToGlm(camera.ViewProjection()) * glm::vec4(x, y, z, 1.0f); // 計算は glm で
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

TEST_CASE("カメラ: カメラの位置が画面の真ん中に映り、カメラが右へ動くと世界は左へずれて見える")
{
    const CameraMatrices camera = husky::engine::MakeOrthographicCamera(1000.0f, 500.0f, 1.0f, 1280.0f, 720.0f);
    const glm::vec4 center = ToClip(camera, 1000.0f, 500.0f);
    CHECK(center.x == doctest::Approx(0.0f));
    CHECK(center.y == doctest::Approx(0.0f));

    // カメラより 640 右、360 上(画面の半分ずつ)の点は、画面の右上の角
    const glm::vec4 topRight = ToClip(camera, 1640.0f, 860.0f);
    CHECK(topRight.x == doctest::Approx(1.0f));
    CHECK(topRight.y == doctest::Approx(1.0f));

    // 同じ点を、カメラを 100 右へ動かして見ると、画面では左(クリップ座標で -100 × 2 / 1280)へずれる
    const CameraMatrices moved = husky::engine::MakeOrthographicCamera(1100.0f, 500.0f, 1.0f, 1280.0f, 720.0f);
    CHECK(ToClip(moved, 1000.0f, 500.0f).x == doctest::Approx(-100.0f * 2.0f / 1280.0f));
}

TEST_CASE("カメラ: zoom 2 なら 2 倍に大きく映り、見える範囲は半分になる。0 以下は 1 として扱う")
{
    const CameraMatrices zoomed = husky::engine::MakeOrthographicCamera(0.0f, 0.0f, 2.0f, 1280.0f, 720.0f);
    // 画面の半分(640 ピクセル)に映るのは、ワールドの 320 単位
    CHECK(ToClip(zoomed, 320.0f, 180.0f).x == doctest::Approx(1.0f));
    CHECK(ToClip(zoomed, 320.0f, 180.0f).y == doctest::Approx(1.0f));

    const CameraMatrices zero = husky::engine::MakeOrthographicCamera(0.0f, 0.0f, 0.0f, 1280.0f, 720.0f);
    CHECK(ToClip(zero, 640.0f, 360.0f).x == doctest::Approx(1.0f));
}

TEST_CASE("カメラ: シーンにカメラが無ければ画面ちょうどのカメラ、あればカメラのアクターの位置と zoom で映す")
{
    husky::engine::Application app;
    husky::engine::Scene& scene = app.GetScene();

    // カメラが無い: 画面ちょうど(ワールドの左下が画面の左下)
    CHECK(ToClip(husky::engine::MakeSceneCamera(scene, 1280.0f, 720.0f), 0.0f, 0.0f).x == doctest::Approx(-1.0f));

    husky::engine::Actor* actor = scene.Spawn<husky::engine::Actor>();
    REQUIRE(actor != nullptr);
    husky::engine::CameraComponent* camera = actor->AddComponent<husky::engine::CameraComponent>();
    REQUIRE(camera != nullptr);
    actor->transform.position.x = 50.0f;
    actor->transform.position.y = 60.0f;
    camera->zoom = 2.0f;
    CHECK(husky::engine::FindSceneCamera(scene) == camera);

    const CameraMatrices fromScene = husky::engine::MakeSceneCamera(scene, 1280.0f, 720.0f);
    const glm::vec4 center = ToClip(fromScene, 50.0f, 60.0f);
    CHECK(center.x == doctest::Approx(0.0f));
    CHECK(center.y == doctest::Approx(0.0f));
    CHECK(ToClip(fromScene, 50.0f + 320.0f, 60.0f).x == doctest::Approx(1.0f)); // zoom 2: 320 単位で画面の端

    // カメラを Destroy すると、また画面ちょうどのカメラに戻る
    actor->Destroy();
    CHECK(husky::engine::FindSceneCamera(scene) == nullptr);
}

TEST_CASE("RS-2: ScreenToWorld は画面の点(左上が原点、下が +y)を、カメラが映しているワールドの位置に戻す")
{
    // ワールドの (1000, 500) を真ん中に、2 倍で映す(見える範囲は 640 x 360)
    const CameraMatrices camera = husky::engine::MakeOrthographicCamera(1000.0f, 500.0f, 2.0f, 1280.0f, 720.0f);
    const husky::core::Vector3 center = husky::engine::ScreenToWorld(camera, 640.0f, 360.0f, 1280.0f, 720.0f);
    CHECK(center.x == doctest::Approx(1000.0f));
    CHECK(center.y == doctest::Approx(500.0f));
    // 画面の左上 = ワールドの左上(x は小さく、y は大きい)
    const husky::core::Vector3 topLeft = husky::engine::ScreenToWorld(camera, 0.0f, 0.0f, 1280.0f, 720.0f);
    CHECK(topLeft.x == doctest::Approx(1000.0f - 320.0f));
    CHECK(topLeft.y == doctest::Approx(500.0f + 180.0f));
    // 行き来: ワールドの点を描くときの変換で画面へ送り、戻すと同じ点になる
    const glm::vec4 clip = ToClip(camera, 1100.0f, 450.0f);
    const float screenX = (clip.x + 1.0f) * 0.5f * 1280.0f;
    const float screenY = (1.0f - clip.y) * 0.5f * 720.0f;
    const husky::core::Vector3 back = husky::engine::ScreenToWorld(camera, screenX, screenY, 1280.0f, 720.0f);
    CHECK(back.x == doctest::Approx(1100.0f));
    CHECK(back.y == doctest::Approx(450.0f));
}

TEST_CASE("RS-2: FindSceneCameraView はシーンのカメラの位置と zoom、無ければ画面ちょうどの見え方")
{
    husky::engine::Application app;
    const husky::engine::CameraView none = husky::engine::FindSceneCameraView(app.GetScene(), 1280.0f, 720.0f);
    CHECK(none.centerX == 640.0f);
    CHECK(none.centerY == 360.0f);
    CHECK(none.zoom == 1.0f);
    husky::engine::Actor* actor = app.GetScene().Spawn<husky::engine::Actor>();
    actor->transform.position = {50.0f, 60.0f, 0.0f};
    actor->AddComponent<husky::engine::CameraComponent>()->zoom = 3.0f;
    const husky::engine::CameraView view = husky::engine::FindSceneCameraView(app.GetScene(), 1280.0f, 720.0f);
    CHECK(view.centerX == 50.0f);
    CHECK(view.centerY == 60.0f);
    CHECK(view.zoom == 3.0f);
}

TEST_CASE("RS-4: WorldToScreen は ScreenToWorld の逆(ワールドの点が映る画面のピクセル)")
{
    const CameraMatrices camera = husky::engine::MakeOrthographicCamera(1000.0f, 500.0f, 2.0f, 1280.0f, 720.0f);
    const husky::core::Vector3 center = husky::engine::WorldToScreen(camera, 1000.0f, 500.0f, 1280.0f, 720.0f);
    CHECK(center.x == doctest::Approx(640.0f));
    CHECK(center.y == doctest::Approx(360.0f));
    const husky::core::Vector3 topLeft = husky::engine::WorldToScreen(camera, 680.0f, 680.0f, 1280.0f, 720.0f);
    CHECK(topLeft.x == doctest::Approx(0.0f));
    CHECK(topLeft.y == doctest::Approx(0.0f));
    const husky::core::Vector3 back = husky::engine::ScreenToWorld(camera, 123.0f, 456.0f, 1280.0f, 720.0f);
    const husky::core::Vector3 again = husky::engine::WorldToScreen(camera, back.x, back.y, 1280.0f, 720.0f);
    CHECK(again.x == doctest::Approx(123.0f));
    CHECK(again.y == doctest::Approx(456.0f));
}
