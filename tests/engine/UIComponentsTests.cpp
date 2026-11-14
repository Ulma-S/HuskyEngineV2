// UI の画像と文字(UIImageComponent、UITextComponent。UI-6a)のテスト。描く位置の計算と保存を確かめる(GPU は使わない)。
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/SceneSerializer.h"
#include "engine/UIActor.h"
#include "engine/UIComponents.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Application;
using husky::engine::CanvasComponent;
using husky::engine::UIActor;
using husky::engine::UIImageComponent;
using husky::engine::UITextComponent;

namespace
{
    // Canvas の下に、右上の角に付けた 200 × 100 のパネルを置く。
    UIActor* SpawnPanel(Application& app)
    {
        UIActor* canvas = app.GetScene().Spawn<UIActor>();
        REQUIRE(canvas != nullptr);
        REQUIRE(canvas->AddComponent<CanvasComponent>() != nullptr);
        UIActor* panel = app.GetScene().Spawn<UIActor>();
        REQUIRE(panel != nullptr);
        panel->name = "Panel";
        panel->rectTransform.anchorMin = {1.0f, 1.0f};
        panel->rectTransform.anchorMax = {1.0f, 1.0f};
        panel->rectTransform.pivot = {1.0f, 1.0f};
        panel->rectTransform.anchoredPosition = {-20.0f, -20.0f};
        panel->rectTransform.sizeDelta = {200.0f, 100.0f};
        REQUIRE(panel->SetParent(canvas, false));
        return panel;
    }
}

TEST_CASE("UIComponents: UIActor の四角の画面での位置(ピクセル)は、Canvas の拡大を掛けたもの")
{
    Application app;
    UIActor* panel = SpawnPanel(app);
    husky::engine::LayoutCanvases(app.GetScene(), 1920.0f, 1080.0f); // 1.5 倍

    const husky::engine::UIScreenRect rect = husky::engine::ComputeUIScreenRect(*panel);
    CHECK(rect.scale == doctest::Approx(1.5f));
    CHECK(rect.width == doctest::Approx(300.0f));
    CHECK(rect.height == doctest::Approx(150.0f));
    CHECK(rect.left == doctest::Approx(1920.0f - 30.0f - 300.0f)); // 右から 20 × 1.5
    CHECK(rect.bottom == doctest::Approx(1080.0f - 30.0f - 150.0f));

    // Canvas そのものは画面いっぱい
    UIActor* canvas = static_cast<UIActor*>(panel->GetParent());
    const husky::engine::UIScreenRect screen = husky::engine::ComputeUIScreenRect(*canvas);
    CHECK(screen.left == doctest::Approx(0.0f));
    CHECK(screen.width == doctest::Approx(1920.0f));
}

TEST_CASE("UIComponents: 字のアトラスは「字の大きさ × Canvas の拡大」を整数に丸めた大きさで作る(くっきり描く)")
{
    CHECK(husky::engine::ComputeUIFontPixelSize(32.0f, 1.5f) == doctest::Approx(48.0f));
    CHECK(husky::engine::ComputeUIFontPixelSize(32.0f, 1.0f) == doctest::Approx(32.0f));
    CHECK(husky::engine::ComputeUIFontPixelSize(20.0f, 1.234f) == doctest::Approx(25.0f)); // 24.68 → 25
    CHECK(husky::engine::ComputeUIFontPixelSize(10.0f, 0.01f) == doctest::Approx(1.0f));  // 小さすぎても 1
}

TEST_CASE("UIComponents: 画像と文字の値は保存して読み戻せる。寄せは名前で保存する")
{
    Application source;
    UIActor* panel = SpawnPanel(source);
    UIImageComponent* image = panel->AddComponent<UIImageComponent>();
    REQUIRE(image != nullptr);
    image->color = {0.2f, 0.7f, 0.3f, 0.8f};
    UITextComponent* text = panel->AddComponent<UITextComponent>();
    REQUIRE(text != nullptr);
    text->text = "スコア: 120";
    text->fontSize = 24.0f;
    text->alignment = husky::engine::TextAlign::Center;
    text->verticalAlignment = husky::engine::UIVerticalAlign::Middle;
    text->wrap = false;

    std::string json;
    REQUIRE(husky::engine::JsonFormat::Write(husky::engine::SceneSerializer::Write(source.GetScene()).value, json));
    CHECK(json.find("\"Center\"") != std::string::npos);
    CHECK(json.find("\"Middle\"") != std::string::npos);

    Application loaded;
    husky::engine::ArchiveValue value;
    REQUIRE(husky::engine::JsonFormat::Read(json, value));
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(value, loaded.GetScene(), loaded.GetTypeRegistry());
    CHECK(report.warnings.empty());
    REQUIRE(loaded.GetScene().actors.size() == 2);
    UIActor* loadedPanel = loaded.GetRegistry().Resolve<UIActor>(loaded.GetScene().actors[1]);
    REQUIRE(loadedPanel != nullptr);
    const UIImageComponent* loadedImage = loadedPanel->GetComponent<UIImageComponent>();
    const UITextComponent* loadedText = loadedPanel->GetComponent<UITextComponent>();
    REQUIRE(loadedImage != nullptr);
    REQUIRE(loadedText != nullptr);
    CHECK(loadedImage->color.g == doctest::Approx(0.7f));
    CHECK(loadedText->text == "スコア: 120");
    CHECK(loadedText->fontSize == doctest::Approx(24.0f));
    CHECK(loadedText->alignment == husky::engine::TextAlign::Center);
    CHECK(loadedText->verticalAlignment == husky::engine::UIVerticalAlign::Middle);
    CHECK_FALSE(loadedText->wrap);
}
