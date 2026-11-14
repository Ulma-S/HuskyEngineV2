// SceneInspector と DrawProperties のテスト(開発ビルドだけ)。
// 画面も GPU も使わずに ImGui を動かし(ヘッドレス)、インスペクタを1フレーム分作らせる。
// ImGui は、使い方の誤り(PushID と PopID の数の食い違い、Begin と End の対応など)を、自分の確認(IM_ASSERT)で止めるので、
// 「最後まで動く」こと自体が、使い方が正しいことの確認になる。マウスでの操作は、ゲームを起動して確かめる。
#if HUSKY_DEVELOPMENT

#include "core/MemoryTracker.h"
#include "engine/Application.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "platform/FileSystem.h"
#include "tools/EditorCommands.h"
#include "tools/Inspector.h"
#include "TestFolder.h"
#include "HeadlessImGui.h"

#include <doctest/doctest.h>
#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::ObjectHandle;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::SpriteComponent;
using husky::tools::SceneInspector;

namespace
{
    using husky::tests::HeadlessImGui;

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


TEST_CASE("Inspector: プレハブから置いたアクターは、プレハブとの違いに印を付け、Revert all / Apply all のボタンを出しても誤りが無い")
{
    HeadlessImGui imgui;
    husky::tests::TestFolder folder("husky_test_inspector_prefab");
    folder.Write("readme.txt", "assets");
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    husky::tools::EditorCommandRegistry commands;
    REQUIRE(commands.Register(husky::tools::MakeCreatePrefabCommand(folder.Root())));
    REQUIRE(commands.Register(husky::tools::MakeSpawnActorCommand()));
    REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
    REQUIRE(commands.Register(husky::tools::MakeSetManyPropertiesCommand()));
    REQUIRE(commands.Register(husky::tools::MakeRevertPrefabCommand()));
    REQUIRE(commands.Register(husky::tools::MakeApplyPrefabCommand(folder.Root())));

    Actor* original = app.GetScene().Spawn<Actor>();
    original->name = "Coin";
    original->AddComponent<SpriteComponent>()->width = 24.0f;
    husky::engine::ArchiveValue args = husky::engine::ArchiveValue::MakeObject();
    args.Set("actor", husky::engine::ArchiveValue("Coin"));
    args.Set("path", husky::engine::ArchiveValue("prefabs/coin.prefab"));
    REQUIRE(commands.Execute("prefab.create", app, args).succeeded);
    husky::engine::ArchiveValue spawnArgs = husky::engine::ArchiveValue::MakeObject();
    spawnArgs.Set("prefab", husky::engine::ArchiveValue("prefabs/coin.prefab"));
    const husky::tools::EditorCommandResult spawned = commands.Execute("actor.spawn", app, spawnArgs);
    REQUIRE(spawned.succeeded);
    husky::core::Guid guid;
    REQUIRE(husky::core::Guid::Parse(spawned.data.Find("guid")->AsString(), guid));
    Actor* placed = app.GetScene().FindActorByGuid(guid);
    REQUIRE(placed != nullptr);

    SceneInspector inspector;
    inspector.Select(placed->GetHandle());
    imgui.Frame([&] { inspector.Draw(app, &commands); });
    CHECK(inspector.GetOverrideCount() == 0);

    // 幅と色の r を変えると、選び直したとき(か 30 フレーム後)に、違いが 2 つになる
    placed->GetComponent<SpriteComponent>()->width = 40.0f;
    placed->GetComponent<SpriteComponent>()->color.r = 0.5f;
    inspector.Select(original->GetHandle());
    imgui.Frame([&] { inspector.Draw(app, &commands); });
    inspector.Select(placed->GetHandle());
    imgui.Frame([&] { inspector.Draw(app, &commands); });
    CHECK(inspector.GetOverrideCount() == 2);
}

TEST_CASE("Inspector: アクターの一覧は親子の木で描き、1000 体でも温まった後は確保しない(HIER-3)")
{
    HeadlessImGui imgui;
    Application app;
    std::vector<Actor*> roots;
    for (int i = 0; i < 1000; ++i)
    {
        Actor* actor = app.GetScene().Spawn<Actor>();
        // 10 体ごとに、直前の根の子(と孫)にする
        if (i % 10 != 0 && !roots.empty())
        {
            REQUIRE(actor->SetParent(roots.back()));
        }
        else
        {
            roots.push_back(actor);
        }
    }
    SceneInspector inspector;
    // 子のアクターを選ぶと、先祖が開いて見えるようになる
    Actor* child = app.GetRegistry().Resolve<Actor>(app.GetScene().actors[15]);
    REQUIRE(child != nullptr);
    inspector.Select(child->GetHandle());
    for (int frame = 0; frame < 3; ++frame)
    {
        imgui.Frame([&] { inspector.Draw(app); });
    }
    inspector.Select(ObjectHandle{}); // 選んでいないとき(プロパティの欄は描かない)に、木だけを測る
    for (int frame = 0; frame < 3; ++frame)
    {
        imgui.Frame([&] { inspector.Draw(app); });
    }
    const std::uint64_t before = husky::core::GetMemoryStats().totalAllocations;
    imgui.Frame([&] { inspector.Draw(app); });
    CHECK(husky::core::GetMemoryStats().totalAllocations == before);
}


#endif
