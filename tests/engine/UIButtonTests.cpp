// UI のボタン(UIButtonComponent、UIEventSystem。UI-6b)のテスト。マウスの入力を外から渡して、押す・離すを再現する。
#include "engine/Application.h"
#include "engine/UIActor.h"
#include "engine/UIComponents.h"

#include <doctest/doctest.h>

using husky::engine::Application;
using husky::engine::CanvasComponent;
using husky::engine::UIActor;
using husky::engine::UIButtonComponent;
using husky::engine::UIButtonState;
using husky::engine::UIImageComponent;
using husky::platform::InputEvents;
using husky::platform::MouseEvent;
using husky::platform::MouseEventType;

namespace
{
    // 押されたこと(OnUIClick)を数えるボタンのアクター
    class CountingButton : public UIActor
    {
        HUSKY_OBJECT_INLINE(CountingButton, UIActor)
    public:
        int clicks = 0;
        void OnUIClick(const husky::engine::UIClickEvent& event) override
        {
            CHECK(event.buttonActor == GetHandle());
            ++clicks;
        }
    };

    // ボタンから名前で呼ばれるメソッドを持つアクター
    class Receiver : public husky::engine::Actor
    {
        HUSKY_OBJECT_INLINE(Receiver, husky::engine::Actor)
    public:
        int starts = 0;
        void StartGame() { ++starts; }
        void Add(int value) { starts += value; } // 引数があるので、ボタンからは呼べない

        HUSKY_REFLECTION_BEGIN()
            HUSKY_METHOD(StartGame)
            HUSKY_METHOD(Add)
        HUSKY_REFLECTION_END()
    };

    struct ButtonScene
    {
        CountingButton* button = nullptr;
        UIButtonComponent* component = nullptr;
        Receiver* receiver = nullptr;
    };

    // Canvas(1280 × 720)の真ん中に 200 × 100 のボタン。押すと Receiver::StartGame を呼ぶ。
    ButtonScene SpawnButtonScene(Application& app)
    {
        ButtonScene scene;
        scene.receiver = app.GetScene().Spawn<Receiver>();
        REQUIRE(scene.receiver != nullptr);
        UIActor* canvas = app.GetScene().Spawn<UIActor>();
        REQUIRE(canvas != nullptr);
        REQUIRE(canvas->AddComponent<CanvasComponent>() != nullptr);
        scene.button = app.GetScene().Spawn<CountingButton>();
        REQUIRE(scene.button != nullptr);
        scene.button->rectTransform.sizeDelta = {200.0f, 100.0f};
        REQUIRE(scene.button->SetParent(canvas, false));
        REQUIRE(scene.button->AddComponent<UIImageComponent>() != nullptr);
        scene.component = scene.button->AddComponent<UIButtonComponent>();
        REQUIRE(scene.component != nullptr);
        scene.component->target.Set(scene.receiver);
        scene.component->method = "StartGame";
        return scene;
    }

    // マウスのイベント1つのフレームの入力(画面は 1280 × 720。位置は画面のピクセル、下が +)。
    InputEvents Mouse(MouseEventType type, float x, float y)
    {
        InputEvents events;
        events.screenWidth = 1280.0f;
        events.screenHeight = 720.0f;
        MouseEvent event;
        event.type = type;
        event.x = x;
        event.y = y;
        events.mouse.push_back(event);
        return events;
    }
}

TEST_CASE("UIButton: 乗ると Hovered、押すと Pressed、ボタンの上で離すとイベントとメソッドが呼ばれる。押したマウスはゲームに見えない")
{
    Application app;
    const ButtonScene scene = SpawnButtonScene(app);
    app.StepFrame(0, Mouse(MouseEventType::Move, 10.0f, 10.0f)); // 1フレーム目で配置される(当たりは前のフレームの配置で調べる)
    CHECK(scene.component->GetState() == UIButtonState::Normal);

    app.StepFrame(0, Mouse(MouseEventType::Move, 640.0f, 360.0f)); // 真ん中へ
    CHECK(scene.component->GetState() == UIButtonState::Hovered);
    CHECK(scene.component->GetTintColor().r == doctest::Approx(scene.component->hoveredColor.r));

    app.StepFrame(0, Mouse(MouseEventType::ButtonDown, 640.0f, 360.0f));
    CHECK(scene.component->GetState() == UIButtonState::Pressed);
    CHECK_FALSE(app.GetInput().WasMousePressed(husky::engine::MouseButton::Left)); // UI が使った
    CHECK(app.GetInput().IsMouseConsumed());
    CHECK(scene.button->clicks == 0); // 押しただけでは呼ばない

    app.StepFrame(0, Mouse(MouseEventType::ButtonUp, 645.0f, 365.0f)); // 少し動いても、ボタンの上で離せば押したことになる
    CHECK(scene.button->clicks == 1);
    CHECK(scene.receiver->starts == 1);
    CHECK(scene.component->GetState() == UIButtonState::Hovered);
}

TEST_CASE("UIButton: 押したまま外へ出て離すと呼ばない。ボタンの外で押したマウスはゲームに渡す")
{
    Application app;
    const ButtonScene scene = SpawnButtonScene(app);
    app.StepFrame(0, Mouse(MouseEventType::Move, 640.0f, 360.0f));
    app.StepFrame(0, Mouse(MouseEventType::ButtonDown, 640.0f, 360.0f));
    app.StepFrame(0, Mouse(MouseEventType::Move, 100.0f, 100.0f));
    CHECK(scene.component->GetState() == UIButtonState::Normal); // 押しているが、外にいる
    CHECK(app.GetInput().IsMouseConsumed());                    // 押している間はゲームに見せない
    app.StepFrame(0, Mouse(MouseEventType::ButtonUp, 100.0f, 100.0f));
    CHECK(scene.button->clicks == 0);
    CHECK(scene.receiver->starts == 0);

    // ボタンの外(UI の無い所)で押すと、ゲームに渡る
    app.StepFrame(0, Mouse(MouseEventType::ButtonDown, 100.0f, 100.0f));
    CHECK(app.GetInput().WasMousePressed(husky::engine::MouseButton::Left));
    CHECK_FALSE(app.GetInput().IsMouseConsumed());
}

TEST_CASE("UIButton: interactable でなければ押せず、色は disabledColor。編集中(Tick を止めている間)も押せない")
{
    Application app;
    const ButtonScene scene = SpawnButtonScene(app);
    scene.component->interactable = false;
    app.StepFrame(0, Mouse(MouseEventType::Move, 640.0f, 360.0f));
    app.StepFrame(0, Mouse(MouseEventType::ButtonDown, 640.0f, 360.0f));
    app.StepFrame(0, Mouse(MouseEventType::ButtonUp, 640.0f, 360.0f));
    CHECK(scene.button->clicks == 0);
    CHECK(scene.component->GetTintColor().r == doctest::Approx(scene.component->disabledColor.r));

    scene.component->interactable = true;
    app.SetSceneTickEnabled(false); // エディタの編集中と同じ
    app.StepFrame(0, Mouse(MouseEventType::ButtonDown, 640.0f, 360.0f));
    app.StepFrame(0, Mouse(MouseEventType::ButtonUp, 640.0f, 360.0f));
    CHECK(scene.button->clicks == 0);
    CHECK(scene.component->GetState() == UIButtonState::Normal);
}

TEST_CASE("UIButton: 呼び先はコンポーネントのメソッドも選べる。無いメソッド・引数のあるメソッドは呼ばずに警告")
{
    Application app;
    const ButtonScene scene = SpawnButtonScene(app);
    // 引数のあるメソッドは呼ばない
    scene.component->method = "Add";
    husky::engine::ClickUIButton(*scene.button, *scene.component);
    CHECK(scene.receiver->starts == 0);
    CHECK(scene.button->clicks == 1); // イベントは届く
    // 無いメソッド
    scene.component->method = "NoSuchMethod";
    husky::engine::ClickUIButton(*scene.button, *scene.component);
    CHECK(scene.receiver->starts == 0);
    // 無いコンポーネント
    scene.component->method = "StartGame";
    scene.component->targetComponent = "CameraComponent";
    husky::engine::ClickUIButton(*scene.button, *scene.component);
    CHECK(scene.receiver->starts == 0);
    // 呼び先が空なら、イベントだけ
    scene.component->targetComponent.clear();
    scene.component->method.clear();
    husky::engine::ClickUIButton(*scene.button, *scene.component);
    CHECK(scene.button->clicks == 4);
    CHECK(scene.receiver->starts == 0);
}
