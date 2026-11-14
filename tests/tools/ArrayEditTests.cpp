// 配列のプロパティの編集(SF-2b)のテスト: インスペクタで描く、記録係で property.set の1件にする、Undo で戻る。開発ビルドだけ。
#if HUSKY_DEVELOPMENT

#include "tools/Inspector.h"
#include "tools/PropertyEditRecorder.h"

#include "core/EnumReflection.h"
#include "core/ObjectRef.h"
#include "engine/Actor.h"
#include "engine/JsonFormat.h"
#include "tools/EditorCommands.h"
#include "HeadlessImGui.h"

#include <doctest/doctest.h>

#include <imgui.h>

#include <string>
#include <vector>

using husky::core::Property;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::tools::EditorCommandRegistry;
using husky::tools::PropertyEditRecorder;

namespace
{
    class Patrol : public Actor
    {
        HUSKY_OBJECT_INLINE(Patrol, Actor)
    public:
        std::vector<float> speeds;
        std::vector<bool> flags;
        std::vector<husky::core::ObjectRef<Actor>> targets;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(speeds)
            HUSKY_PROPERTY(flags)
            HUSKY_PROPERTY(targets)
        HUSKY_REFLECTION_END()
    };

    void RegisterCommands(EditorCommandRegistry& commands)
    {
        REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
        REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    }

    // インスペクタが配列の要素1つを描くのと同じ順で、記録係を呼ぶ(edit が部品の書き換えの代わり)。
    template <typename Edit>
    void DrawElement(PropertyEditRecorder& recorder, Application& app, EditorCommandRegistry& commands, Patrol& patrol,
                     std::size_t index, Edit&& edit, bool active)
    {
        const Property& speeds = *patrol.GetType().FindProperty("speeds");
        const husky::core::ArrayInfo& array = *speeds.GetArrayInfo();
        void* element = array.at(speeds.GetArrayAddressAt(&patrol), index);
        recorder.BeginFrame(app, &commands);
        recorder.SetOwner(patrol);
        recorder.PushPath(speeds);
        recorder.BeforeLeaf(speeds, &patrol);
        recorder.EnterArray();
        recorder.BeforeLeaf(array.element, element);
        edit();
        recorder.AfterLeaf(array.element, element, true, active);
        recorder.LeaveArray();
        recorder.AfterLeaf(speeds, &patrol, false, false); // 配列そのものは値1つではないので記録しない
        recorder.PopPath();
        recorder.EndFrame();
    }
}

TEST_CASE("SF-2b: property.set は配列まるごとを受け付ける(参照の配列は REF-1 の配列のテストで確かめる)")
{
    Application app;
    Patrol* patrol = app.GetScene().Spawn<Patrol>();
    patrol->name = "Patrol";
    EditorCommandRegistry commands;
    RegisterCommands(commands);

    ArchiveValue args;
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Patrol", "property": "speeds", "value": [1, 2.5]})", args));
    REQUIRE(commands.Execute("property.set", app, args).succeeded);
    CHECK(patrol->speeds == std::vector<float>{1.0f, 2.5f});
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Patrol", "property": "flags", "value": [true, false]})", args));
    REQUIRE(commands.Execute("property.set", app, args).succeeded);
    CHECK(patrol->flags == std::vector<bool>{true, false});

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(patrol->flags.empty());
}

TEST_CASE("SF-2b: 配列の要素のドラッグは、手を離したときに配列まるごとの property.set の1件になり、Undo で戻る")
{
    Application app;
    Patrol* patrol = app.GetScene().Spawn<Patrol>();
    patrol->speeds = {1.0f, 2.0f};
    EditorCommandRegistry commands;
    RegisterCommands(commands);
    PropertyEditRecorder recorder;

    DrawElement(recorder, app, commands, *patrol, 1, [&] { patrol->speeds[1] = 3.0f; }, true);  // ドラッグ中
    DrawElement(recorder, app, commands, *patrol, 1, [&] { patrol->speeds[1] = 4.0f; }, false); // 手を離した
    CHECK(patrol->speeds == std::vector<float>{1.0f, 4.0f});
    CHECK(commands.GetUndoCount() == 1);

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(patrol->speeds == std::vector<float>{1.0f, 2.0f}); // ドラッグを始める前の配列に戻る
}

TEST_CASE("SF-2b: 要素を足す・消すは、すぐに配列まるごとの property.set になり、Undo で戻る")
{
    Application app;
    Patrol* patrol = app.GetScene().Spawn<Patrol>();
    patrol->speeds = {1.0f, 2.0f, 3.0f};
    EditorCommandRegistry commands;
    RegisterCommands(commands);
    PropertyEditRecorder recorder;
    const Property& speeds = *patrol->GetType().FindProperty("speeds");

    recorder.BeginFrame(app, &commands);
    recorder.SetOwner(*patrol);
    recorder.PushPath(speeds);
    recorder.EnterArray();
    recorder.BeginWholeChange();
    speeds.GetArrayInfo()->erase(speeds.GetArrayAddressAt(patrol), 0); // 「-」を押した
    recorder.CommitWholeChange();
    recorder.LeaveArray();
    recorder.PopPath();
    recorder.EndFrame();
    CHECK(patrol->speeds == std::vector<float>{2.0f, 3.0f});
    CHECK(commands.GetUndoCount() == 1);

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(patrol->speeds == std::vector<float>{1.0f, 2.0f, 3.0f});
}

TEST_CASE("SF-2b: インスペクタは配列を開いて要素を並べても、ImGui の使い方の誤りが無く、値も変えない")
{
    husky::tests::HeadlessImGui imgui;
    Application app;
    Patrol* patrol = app.GetScene().Spawn<Patrol>();
    patrol->speeds = {1.0f, 2.0f};
    patrol->flags = {true, false};
    patrol->targets.resize(1);
    patrol->targets[0].Set(app.GetScene().Spawn<Actor>());

    bool changed = true;
    for (int frame = 0; frame < 2; ++frame)
    {
        imgui.Frame([&] {
            ImGui::Begin("Test");
            // 3つの配列の見出しを開いた状態にする(見出しの ID は、プロパティの名前の PushID の中の、同じ名前)
            for (const char* name : {"speeds", "flags", "targets"})
            {
                ImGui::PushID(name);
                ImGui::GetStateStorage()->SetInt(ImGui::GetID(name), 1);
                ImGui::PopID();
            }
            changed = husky::tools::DrawProperties(patrol->GetType(), patrol, app.GetRegistry());
            ImGui::End();
        });
    }
    CHECK_FALSE(changed);
    CHECK(patrol->speeds == std::vector<float>{1.0f, 2.0f});
    CHECK(patrol->flags == std::vector<bool>{true, false});
}
namespace
{
    enum class Mood
    {
        Calm,
        Angry,
    };
    HUSKY_ENUM_BEGIN(Mood)
        HUSKY_ENUM_VALUE(Calm)
        HUSKY_ENUM_VALUE(Angry)
    HUSKY_ENUM_END()

    class Guard : public Actor
    {
        HUSKY_OBJECT_INLINE(Guard, Actor)
    public:
        Mood mood = Mood::Calm;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(mood)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("SF-3b: property.set は enum を値の名前で書き換え、Undo で戻る。インスペクタは選択肢で描く")
{
    Application app;
    Guard* guard = app.GetScene().Spawn<Guard>();
    guard->name = "Guard";
    EditorCommandRegistry commands;
    RegisterCommands(commands);

    ArchiveValue args;
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Guard", "property": "mood", "value": "Angry"})", args));
    REQUIRE(commands.Execute("property.set", app, args).succeeded);
    CHECK(guard->mood == Mood::Angry);
    REQUIRE(husky::engine::JsonFormat::Read(R"({"actor": "Guard", "property": "mood", "value": "Sleepy"})", args));
    CHECK_FALSE(commands.Execute("property.set", app, args).succeeded); // 知らない名前
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(guard->mood == Mood::Calm);

    husky::tests::HeadlessImGui imgui;
    bool changed = true;
    imgui.Frame([&] {
        ImGui::Begin("Test");
        changed = husky::tools::DrawProperties(guard->GetType(), guard, app.GetRegistry());
        ImGui::End();
    });
    CHECK_FALSE(changed);
}
#endif
