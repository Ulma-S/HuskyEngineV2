// ゲーム用 UI の描くパスの分け方と、エディタでの扱い(UI-5b)のテスト。
// 描くパスは engine(どのビルドでも)、エディタの部分は開発ビルドだけ。
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/SpriteComponent.h"
#include "engine/UIActor.h"

#include <doctest/doctest.h>

#include <vector>

using husky::engine::Application;
using husky::engine::CanvasComponent;
using husky::engine::SpriteComponent;
using husky::engine::SpritePass;
using husky::engine::UIActor;

namespace
{
    // Canvas(UIActor + CanvasComponent)、その下の UIActor(絵つき)、Canvas の外の UIActor(絵つき)、ふつうのアクター(絵つき)。
    struct UIScene
    {
        UIActor* canvas = nullptr;
        UIActor* label = nullptr;
        UIActor* lonely = nullptr;
        husky::engine::Actor* world = nullptr;
    };

    UIScene SpawnUIScene(Application& app)
    {
        UIScene scene;
        scene.world = app.GetScene().Spawn<husky::engine::Actor>();
        REQUIRE(scene.world != nullptr);
        REQUIRE(scene.world->AddComponent<SpriteComponent>() != nullptr);
        scene.canvas = app.GetScene().Spawn<UIActor>();
        REQUIRE(scene.canvas != nullptr);
        REQUIRE(scene.canvas->AddComponent<CanvasComponent>() != nullptr);
        scene.label = app.GetScene().Spawn<UIActor>();
        REQUIRE(scene.label != nullptr);
        REQUIRE(scene.label->AddComponent<SpriteComponent>() != nullptr);
        scene.label->name = "Label";
        REQUIRE(scene.label->SetParent(scene.canvas, false));
        scene.lonely = app.GetScene().Spawn<UIActor>();
        REQUIRE(scene.lonely != nullptr);
        REQUIRE(scene.lonely->AddComponent<SpriteComponent>() != nullptr);
        husky::engine::LayoutCanvases(app.GetScene(), 1280.0f, 720.0f);
        return scene;
    }

    std::vector<husky::engine::Actor*> SpriteOwners(Application& app, SpritePass pass)
    {
        std::vector<husky::engine::Actor*> owners;
        husky::engine::ForEachSprite(
            app.GetScene(), [&](husky::render::Sprite&, SpriteComponent& component) { owners.push_back(component.GetOwner()); },
            pass);
        return owners;
    }
}

TEST_CASE("UIEditor: Canvas の下の UIActor は画面のパスでだけ描き、ワールドのパスでは描かない。Canvas の外の UIActor はどちらでも描かない")
{
    Application app;
    const UIScene scene = SpawnUIScene(app);

    const std::vector<husky::engine::Actor*> world = SpriteOwners(app, SpritePass::World);
    REQUIRE(world.size() == 1);
    CHECK(world[0] == scene.world);

    const std::vector<husky::engine::Actor*> screen = SpriteOwners(app, SpritePass::Screen);
    REQUIRE(screen.size() == 1);
    CHECK(screen[0] == scene.label);
    CHECK(scene.label->IsDrawnOnScreen());
    CHECK_FALSE(scene.lonely->IsDrawnOnScreen());

    // シーンビューのクリックで選ぶ当たり(ワールド)にも、UI は入らない
    CHECK(husky::engine::FindSpriteActorAt(app.GetScene(), scene.label->GetWorldPosition().x,
                                           scene.label->GetWorldPosition().y) != scene.label);
}

#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"
#include "tools/UIRectOverlay.h"

namespace
{
    husky::engine::ArchiveValue ParseJson(const char* text)
    {
        husky::engine::ArchiveValue value;
        REQUIRE(husky::engine::JsonFormat::Read(text, value));
        return value;
    }
}

TEST_CASE("UIEditor: property.set は UIActor の transform(計算の結果)を断り、rectTransform は書ける")
{
    Application app;
    const UIScene scene = SpawnUIScene(app);
    husky::tools::EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));

    const husky::tools::EditorCommandResult refused = commands.Execute(
        "property.set", app, ParseJson(R"({"actor": "Label", "property": "transform.position.x", "value": 50})"));
    CHECK_FALSE(refused.succeeded);
    CHECK(refused.message.find("rectTransform") != std::string::npos);

    const husky::tools::EditorCommandResult written = commands.Execute(
        "property.set", app, ParseJson(R"({"actor": "Label", "property": "rectTransform.anchoredPosition.x", "value": 50})"));
    CHECK(written.succeeded);
    CHECK(scene.label->rectTransform.anchoredPosition.x == doctest::Approx(50.0f));
}

TEST_CASE("UIEditor: UI の四角の枠は、Canvas の下の UIActor ごとに 4 本の線。選んでいるものは最後に橙色")
{
    Application app;
    const UIScene scene = SpawnUIScene(app);

    std::vector<husky::render::Sprite> lines;
    husky::tools::BuildUIRectOutlines(app.GetScene(), scene.label->GetHandle(), lines);
    REQUIRE(lines.size() == 8); // Canvas と Label(Canvas の外の UIActor とふつうのアクターは無し)
    CHECK(lines.back().color.r == doctest::Approx(1.0f)); // 選んでいる Label が最後(橙色)
    CHECK(lines.back().color.b < 0.5f);
    CHECK(lines.front().color.r < 1.0f); // Canvas は灰色

    // Label の枠: 既定の 100 × 100 が Canvas の真ん中(640, 360)にあるので、線は 590〜690 の間
    for (std::size_t i = 4; i < 8; ++i)
    {
        const husky::render::SpriteCorners corners = husky::render::ComputeSpriteCorners(lines[i]);
        for (int c = 0; c < 4; ++c)
        {
            CHECK(corners.x[c] > 585.0f);
            CHECK(corners.x[c] < 695.0f);
        }
    }
}

#endif
