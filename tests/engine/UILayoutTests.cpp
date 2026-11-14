// ゲーム用 UI の配置(RectTransform、UIActor、CanvasComponent、LayoutCanvases。UI-5a)のテスト。GPU は使わない。
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/SceneSerializer.h"
#include "engine/UIActor.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Application;
using husky::engine::CanvasComponent;
using husky::engine::ComputeChildRect;
using husky::engine::LayoutCanvases;
using husky::engine::RectTransform;
using husky::engine::UIActor;
using husky::engine::UIRect;

namespace
{
    RectTransform MakeRect(float anchorX, float anchorY, float pivotX, float pivotY, float x, float y, float width,
                           float height)
    {
        RectTransform rect;
        rect.anchorMin = {anchorX, anchorY};
        rect.anchorMax = {anchorX, anchorY};
        rect.pivot = {pivotX, pivotY};
        rect.anchoredPosition = {x, y};
        rect.sizeDelta = {width, height};
        return rect;
    }

    // Canvas(1280 × 720)の根を1つ作る。
    UIActor* SpawnCanvas(Application& app)
    {
        UIActor* canvas = app.GetWorld().Spawn<UIActor>();
        REQUIRE(canvas != nullptr);
        REQUIRE(canvas->AddComponent<CanvasComponent>() != nullptr);
        return canvas;
    }

    UIActor* SpawnChild(Application& app, UIActor& parent, const RectTransform& rect)
    {
        UIActor* child = app.GetWorld().Spawn<UIActor>();
        REQUIRE(child != nullptr);
        child->rectTransform = rect;
        REQUIRE(child->SetParent(&parent, false));
        return child;
    }
}

TEST_CASE("UILayout: アンカーが1点なら、sizeDelta が大きさで、pivot がアンカーの点 + anchoredPosition に来る")
{
    const UIRect parent{0.0f, 0.0f, 1280.0f, 720.0f};
    // 真ん中に付け、右へ 10・上へ 20 ずらした 100 × 50(pivot は真ん中)
    const UIRect center = ComputeChildRect(parent, MakeRect(0.5f, 0.5f, 0.5f, 0.5f, 10.0f, 20.0f, 100.0f, 50.0f));
    CHECK(center.x == doctest::Approx(600.0f)); // 640 + 10 - 50
    CHECK(center.y == doctest::Approx(355.0f)); // 360 + 20 - 25
    CHECK(center.width == doctest::Approx(100.0f));
    CHECK(center.height == doctest::Approx(50.0f));

    // 左上に付け、pivot も左上: 右へ 16・下へ 16 の所に左上の角が来る
    const UIRect topLeft = ComputeChildRect(parent, MakeRect(0.0f, 1.0f, 0.0f, 1.0f, 16.0f, -16.0f, 100.0f, 30.0f));
    CHECK(topLeft.x == doctest::Approx(16.0f));
    CHECK(topLeft.y == doctest::Approx(720.0f - 16.0f - 30.0f));

    // 親が原点にない四角でも、親の中の割合で付く
    const UIRect inner = ComputeChildRect(UIRect{100.0f, 50.0f, 200.0f, 100.0f}, MakeRect(1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 20.0f, 10.0f));
    CHECK(inner.x == doctest::Approx(280.0f)); // 親の右下の角に、自分の右下の角
    CHECK(inner.y == doctest::Approx(50.0f));
}

TEST_CASE("UILayout: アンカーが2点なら、親の四角の一部に合わせて伸び、sizeDelta はそれより大きい分")
{
    const UIRect parent{0.0f, 0.0f, 1280.0f, 720.0f};
    // 下の辺いっぱい(左右に 10 ずつ余白)、高さ 40 の帯
    RectTransform bar;
    bar.anchorMin = {0.0f, 0.0f};
    bar.anchorMax = {1.0f, 0.0f};
    bar.pivot = {0.5f, 0.0f};
    bar.anchoredPosition = {0.0f, 0.0f};
    bar.sizeDelta = {-20.0f, 40.0f};
    const UIRect result = ComputeChildRect(parent, bar);
    CHECK(result.x == doctest::Approx(10.0f));
    CHECK(result.y == doctest::Approx(0.0f));
    CHECK(result.width == doctest::Approx(1260.0f));
    CHECK(result.height == doctest::Approx(40.0f));

    // 親いっぱい(アンカー (0,0)〜(1,1)、sizeDelta 0)は親と同じ四角
    RectTransform fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    fill.sizeDelta = {0.0f, 0.0f};
    const UIRect filled = ComputeChildRect(parent, fill);
    CHECK(filled.x == doctest::Approx(0.0f));
    CHECK(filled.width == doctest::Approx(1280.0f));
    CHECK(filled.height == doctest::Approx(720.0f));
}

TEST_CASE("UILayout: Canvas は画面の高さに合わせて拡大し、子のワールドの位置はそのまま画面のピクセルになる")
{
    Application app;
    UIActor* canvas = SpawnCanvas(app);
    // 右上の角から左へ 20・下へ 20 に右上の角を合わせた 200 × 100
    UIActor* panel = SpawnChild(app, *canvas, MakeRect(1.0f, 1.0f, 1.0f, 1.0f, -20.0f, -20.0f, 200.0f, 100.0f));
    // パネルの真ん中に 50 × 50
    UIActor* icon = SpawnChild(app, *panel, MakeRect(0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 50.0f, 50.0f));

    LayoutCanvases(app.GetWorld(), 1920.0f, 1080.0f); // 1.5 倍
    REQUIRE(canvas->IsInCanvas());
    CHECK(canvas->GetRect().width == doctest::Approx(1280.0f));
    CHECK(canvas->transform.scale.x == doctest::Approx(1.5f));
    REQUIRE(panel->IsInCanvas());
    CHECK(panel->GetRect().x == doctest::Approx(1280.0f - 20.0f - 200.0f));
    CHECK(panel->GetRect().y == doctest::Approx(720.0f - 20.0f - 100.0f));
    // パネルの pivot(右上の角)のワールドの位置 = Canvas の単位 × 1.5 = 画面のピクセル
    CHECK(panel->GetWorldPosition().x == doctest::Approx((1280.0f - 20.0f) * 1.5f));
    CHECK(panel->GetWorldPosition().y == doctest::Approx((720.0f - 20.0f) * 1.5f));
    // アイコンの真ん中 = パネルの真ん中
    REQUIRE(icon->IsInCanvas());
    CHECK(icon->GetWorldPosition().x == doctest::Approx((1280.0f - 20.0f - 100.0f) * 1.5f));
    CHECK(icon->GetWorldPosition().y == doctest::Approx((720.0f - 20.0f - 50.0f) * 1.5f));

    // 横長の画面(2560 × 1080): 高さに合わせるので拡大は同じ 1.5、Canvas の幅が広がり、右上に付けたものは右上に付く
    LayoutCanvases(app.GetWorld(), 2560.0f, 1080.0f);
    CHECK(canvas->GetRect().width == doctest::Approx(2560.0f / 1.5f));
    CHECK(panel->GetWorldPosition().x == doctest::Approx(2560.0f - 20.0f * 1.5f));
}

TEST_CASE("UILayout: Canvas の外の UIActor は置かない。画面の大きさが 0 なら基準の解像度")
{
    Application app;
    UIActor* lonely = app.GetWorld().Spawn<UIActor>(); // Canvas の下ではない
    REQUIRE(lonely != nullptr);
    UIActor* canvas = SpawnCanvas(app);
    UIActor* child = SpawnChild(app, *canvas, MakeRect(0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 10.0f, 10.0f));

    LayoutCanvases(app.GetWorld(), 0.0f, 0.0f);
    CHECK_FALSE(lonely->IsInCanvas());
    REQUIRE(child->IsInCanvas());
    CHECK(canvas->transform.scale.x == doctest::Approx(1.0f));
    CHECK(child->GetWorldPosition().x == doctest::Approx(5.0f));

    // StepFrame も毎フレーム置く(テストは画面の大きさを渡さないので、基準の解像度)
    child->rectTransform.anchoredPosition = {30.0f, 40.0f};
    app.StepFrame(0, {});
    CHECK(child->GetWorldPosition().x == doctest::Approx(30.0f));
    CHECK(child->GetWorldPosition().y == doctest::Approx(40.0f));
}

TEST_CASE("UILayout: シーンのファイルには RectTransform を書き、transform(計算の結果)は書かない。読み戻して置き直せる")
{
    Application source;
    UIActor* canvas = SpawnCanvas(source);
    UIActor* child = SpawnChild(source, *canvas, MakeRect(0.0f, 1.0f, 0.0f, 1.0f, 16.0f, -16.0f, 120.0f, 40.0f));
    child->name = "Label";
    LayoutCanvases(source.GetWorld(), 1920.0f, 1080.0f); // transform に値が入った状態で書く

    const husky::engine::SceneSerializer::WriteResult saved = husky::engine::SceneSerializer::Write(source.GetWorld());
    CHECK(saved.warnings.empty());
    std::string text;
    REQUIRE(husky::engine::JsonFormat::Write(saved.value, text));
    CHECK(text.find("\"transform\"") == std::string::npos);
    CHECK(text.find("\"rectTransform\"") != std::string::npos);
    CHECK(text.find("CanvasComponent") != std::string::npos);

    Application loaded;
    husky::engine::ArchiveValue value;
    REQUIRE(husky::engine::JsonFormat::Read(text, value));
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(value, loaded.GetWorld(), loaded.GetTypeRegistry());
    CHECK(report.warnings.empty());
    REQUIRE(loaded.GetWorld().actors.size() == 2);
    UIActor* loadedChild = loaded.GetRegistry().Resolve<UIActor>(loaded.GetWorld().actors[1]);
    REQUIRE(loadedChild != nullptr);
    CHECK(loadedChild->name == "Label");
    CHECK(loadedChild->rectTransform.sizeDelta.x == doctest::Approx(120.0f));
    LayoutCanvases(loaded.GetWorld(), 1280.0f, 720.0f);
    REQUIRE(loadedChild->IsInCanvas());
    CHECK(loadedChild->GetRect().x == doctest::Approx(16.0f));
    CHECK(loadedChild->GetRect().y == doctest::Approx(720.0f - 16.0f - 40.0f));
}
