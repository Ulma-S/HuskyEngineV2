// SceneInspector と DrawProperties のテスト(開発ビルドだけ)。
// 画面も GPU も使わずに ImGui を動かし(ヘッドレス)、インスペクタを1フレーム分作らせる。
// ImGui は、使い方の誤り(PushID と PopID の数の食い違い、Begin と End の対応など)を、自分の確認(IM_ASSERT)で止めるので、
// 「最後まで動く」こと自体が、使い方が正しいことの確認になる。マウスでの操作は、ゲームを起動して確かめる。
#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "platform/FileSystem.h"
#include "tools/Inspector.h"

#include <doctest/doctest.h>
#include <imgui.h>

#include <cstdint>
#include <string>

using husky::core::ObjectHandle;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::SpriteComponent;
using husky::tools::SceneInspector;

namespace
{
    // 画面を持たない ImGui を1つ用意し、テストが終わったら片付ける(コンストラクタで用意し、デストラクタで片付ける RAII)。
    class HeadlessImGui
    {
    public:
        HeadlessImGui()
        {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            // 「テクスチャの作成は描画の部品が受け持つ」と伝える(実際には何もしない)。こうすると、
            // フォントの画像を GPU に送る部品が無くても、ImGui はフレームを進められる。
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        }
        ~HeadlessImGui() { ImGui::DestroyContext(); }

        HeadlessImGui(const HeadlessImGui&) = delete;
        HeadlessImGui& operator=(const HeadlessImGui&) = delete;

        // 1フレーム分: NewFrame → draw() で UI を作る → Render。
        template <typename Function>
        void Frame(Function&& draw)
        {
            ImGui::NewFrame();
            draw();
            ImGui::Render();
        }
    };

    class Chaser : public Actor
    {
        HUSKY_OBJECT(Chaser, Actor)
    public:
        bool active = true;
        std::int32_t level = 3;
        ObjectHandle target;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(active)
            HUSKY_PROPERTY(level)
            HUSKY_PROPERTY(target)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("Inspector: すべての種類のプロパティを並べても、ImGui の使い方の誤りが無い。並べるだけでは値は変わらない")
{
    HeadlessImGui imgui;
    Application app;
    Chaser* chaser = app.GetScene().Spawn<Chaser>();
    chaser->name = "chaser";
    chaser->target = app.GetScene().Spawn<Actor>()->GetHandle();
    SpriteComponent* sprite = chaser->AddComponent<SpriteComponent>(); // float と構造体(色)
    sprite->color = {0.1f, 0.2f, 0.3f, 0.4f};

    bool changed = true;
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(chaser->GetType(), chaser, app.GetRegistry());
        changed |= husky::tools::DrawProperties(sprite->GetType(), sprite, app.GetRegistry(), &app.GetAssetDatabase()); // テクスチャの選択の箱も
        ImGui::End();
    });

    CHECK_FALSE(changed);
    CHECK(chaser->level == 3);
    CHECK(sprite->color.b == 0.3f);
}

TEST_CASE("Inspector: 1000 体のシーンで、選んだアクターとコンポーネントを出せる。消えたアクターの選択は、選んでいないのと同じに扱う")
{
    HeadlessImGui imgui;
    Application app;
    for (int i = 0; i < 1000; ++i)
    {
        app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    }
    Actor* picked = app.GetRegistry().Resolve<Actor>(app.GetScene().actors[500]);
    REQUIRE(picked != nullptr);

    SceneInspector inspector;
    inspector.Select(picked->GetHandle());
    imgui.Frame([&] { inspector.Draw(app); }); // 選んだアクターがいるとき
    CHECK(inspector.GetSelected() == picked->GetHandle());

    picked->Destroy();
    app.GetClock().SetFixedDelta(0.01);
    app.StepFrame(0, {}); // フレームの終わりに消える
    imgui.Frame([&] { inspector.Draw(app); }); // 選んだアクターが消えた後も、落ちずに「選んでいない」と出す
    CHECK(app.GetRegistry().Resolve(inspector.GetSelected()) == nullptr);
}

TEST_CASE("Inspector: 切り出しの ID は、差し替えた表示(名前の選択肢)で並べる。差し替えは、ある名前のプロパティにだけ登録できる")
{
    HeadlessImGui imgui;
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    const husky::core::Guid* shapes = app.GetAssetDatabase().FindGuid("sprites/shapes.spritesheet");
    REQUIRE(shapes != nullptr);

    SpriteComponent* sprite = app.GetScene().Spawn<Actor>()->AddComponent<SpriteComponent>();
    sprite->sheet.guid = *shapes;
    sprite->sprite = 1917386538; // diamond

    // スプライトシートを読む前(ID の数字だけを出す)と、読んだ後(名前の選択肢を出す)の両方で、ImGui の使い方の誤りが無い。
    bool changed = true;
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(sprite->GetType(), sprite, app.GetRegistry(), &app.GetAssetDatabase());
        ImGui::End();
    });
    CHECK_FALSE(changed);

    app.GetAssetLoader().Resolve(sprite->sheet);
    app.GetAssetLoader().WaitForAllLoads();
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(sprite->GetType(), sprite, app.GetRegistry(), &app.GetAssetDatabase());
        ImGui::End();
    });
    CHECK_FALSE(changed);
    CHECK(sprite->sprite == 1917386538);

    CHECK_FALSE(husky::tools::RegisterPropertyDrawer(SpriteComponent::StaticType(), "no_such_property", nullptr));
}

TEST_CASE("Inspector: アニメーションの ID は、同じアクターのスプライトシートのアニメーションの名前から選ぶ表示で並べる")
{
    HeadlessImGui imgui;
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(husky::platform::GetExecutableDirectory() + "assets"));
    const husky::core::Guid* shapes = app.GetAssetDatabase().FindGuid("sprites/shapes.spritesheet");
    REQUIRE(shapes != nullptr);

    Actor* actor = app.GetScene().Spawn<Actor>();
    SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
    sprite->sheet.guid = *shapes;
    auto* animator = actor->AddComponent<husky::engine::SpriteAnimatorComponent>();
    animator->Play(1811624963); // cycle

    // 読む前(ID の数字だけ)と、読んだ後(名前の選択肢)の両方で、ImGui の使い方の誤りが無い。
    bool changed = true;
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(animator->GetType(), animator, app.GetRegistry(), &app.GetAssetDatabase());
        ImGui::End();
    });
    CHECK_FALSE(changed);

    app.GetAssetLoader().Resolve(sprite->sheet);
    app.GetAssetLoader().WaitForAllLoads();
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(animator->GetType(), animator, app.GetRegistry(), &app.GetAssetDatabase());
        ImGui::End();
    });
    CHECK_FALSE(changed);
    CHECK(animator->animation == 1811624963);

    // SpriteComponent の無いアクターのアニメーターも、落ちずに表示できる。
    auto* lonely = app.GetScene().Spawn<Actor>()->AddComponent<husky::engine::SpriteAnimatorComponent>();
    imgui.Frame([&] {
        ImGui::Begin("Test");
        husky::tools::DrawProperties(lonely->GetType(), lonely, app.GetRegistry(), &app.GetAssetDatabase());
        ImGui::End();
    });
}

#endif
