// 参照のプロパティの編集(REF-1)のテスト: コマンドの引数の形、property.set、インスペクタの記録係と Undo。開発ビルドだけ。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"
#include "tools/Inspector.h"
#include "tools/PropertyEditRecorder.h"

#include "core/ObjectRef.h"
#include "engine/Actor.h"
#include "engine/JsonFormat.h"
#include "engine/SpriteComponent.h"
#include "HeadlessImGui.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::core::ObjectHandle;
using husky::core::ObjectRef;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::engine::SpriteComponent;
using husky::tools::EditorCommandRegistry;

namespace
{
    class Prey : public Actor
    {
        HUSKY_OBJECT_INLINE(Prey, Actor)
    };

    class Hunter : public Actor
    {
        HUSKY_OBJECT_INLINE(Hunter, Actor)
    public:
        ObjectRef<Prey> prey;
        ObjectRef<SpriteComponent> sprite;
        ObjectHandle anything;
        std::vector<ObjectRef<Prey>> flock; // 参照の配列

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(prey)
            HUSKY_PROPERTY(sprite)
            HUSKY_PROPERTY(anything)
            HUSKY_PROPERTY(flock)
        HUSKY_REFLECTION_END()
    };

    void RegisterCommands(EditorCommandRegistry& commands)
    {
        REQUIRE(commands.Register(husky::tools::MakeSetPropertyCommand()));
        REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
    }

    husky::tools::EditorCommandResult SetProperty(EditorCommandRegistry& commands, Application& app, const std::string& json)
    {
        ArchiveValue args;
        REQUIRE(husky::engine::JsonFormat::Read(json, args));
        return commands.Execute("property.set", app, args);
    }
}

TEST_CASE("REF-1: 参照はコマンドの引数では、アクターは guid、コンポーネントは {actor, component, index} で表し、引き直せる")
{
    Application app;
    Prey* prey = app.GetWorld().Spawn<Prey>();
    prey->name = "Rabbit";
    SpriteComponent* first = prey->AddComponent<SpriteComponent>();
    SpriteComponent* second = prey->AddComponent<SpriteComponent>();

    const ArchiveValue actorArg = husky::tools::MakeReferenceArg(app, prey->GetHandle());
    REQUIRE(actorArg.GetType() == ArchiveValue::Type::String);
    CHECK(actorArg.AsString() == prey->guid.ToString());
    const ArchiveValue secondArg = husky::tools::MakeReferenceArg(app, second->GetHandle());
    REQUIRE(secondArg.Find("component") != nullptr);
    CHECK(secondArg.Find("component")->AsString() == "SpriteComponent");
    CHECK(secondArg.Find("index")->AsInt() == 1); // 同じ型の 2 つ目
    CHECK(husky::tools::MakeReferenceArg(app, first->GetHandle()).Find("index") == nullptr);
    CHECK(husky::tools::MakeReferenceArg(app, ObjectHandle{}).IsNull());

    ObjectHandle found;
    std::string error;
    REQUIRE(husky::tools::FindReferenceFromArg(app, secondArg, found, error));
    CHECK(found == second->GetHandle());
    REQUIRE(husky::tools::FindReferenceFromArg(app, ArchiveValue("Rabbit"), found, error)); // 名前でも指せる
    CHECK(found == prey->GetHandle());
    REQUIRE(husky::tools::FindReferenceFromArg(app, ArchiveValue(), found, error));
    CHECK(found.IsNull());
    CHECK_FALSE(husky::tools::FindReferenceFromArg(app, ArchiveValue("Nobody"), found, error));
}

TEST_CASE("REF-1: property.set で参照を書き換えられる。型の違う相手は断り、null で外し、Undo で戻る")
{
    Application app;
    Hunter* hunter = app.GetWorld().Spawn<Hunter>();
    hunter->name = "Hunter";
    Prey* prey = app.GetWorld().Spawn<Prey>();
    prey->name = "Rabbit";
    SpriteComponent* sprite = prey->AddComponent<SpriteComponent>();
    EditorCommandRegistry commands;
    RegisterCommands(commands);

    REQUIRE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "prey", "value": "Rabbit"})").succeeded);
    CHECK(hunter->prey.Resolve(app.GetRegistry()) == prey);
    // Hunter は Prey ではないので断り、前の相手のまま
    CHECK_FALSE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "prey", "value": "Hunter"})").succeeded);
    CHECK(hunter->prey.Resolve(app.GetRegistry()) == prey);
    // コンポーネントを指す
    REQUIRE(SetProperty(commands, app,
                        R"({"actor": "Hunter", "property": "sprite", "value": {"actor": "Rabbit", "component": "SpriteComponent"}})")
                .succeeded);
    CHECK(hunter->sprite.Resolve(app.GetRegistry()) == sprite);
    // 型の無い参照は、アクターなら何でも
    REQUIRE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "anything", "value": "Hunter"})").succeeded);
    CHECK(hunter->anything == hunter->GetHandle());
    // null で外す
    REQUIRE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "prey", "value": null})").succeeded);
    CHECK(hunter->prey.IsNull());

    // Undo は新しい順に戻る(prey → Rabbit、anything → 無し、sprite → 無し)
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(hunter->prey.Resolve(app.GetRegistry()) == prey);
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(hunter->anything.IsNull());
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(hunter->sprite.IsNull());
}

TEST_CASE("REF-1: インスペクタで相手を選ぶと、記録係が property.set の1件にし、Undo で前の相手に戻る")
{
    Application app;
    Hunter* hunter = app.GetWorld().Spawn<Hunter>();
    Prey* first = app.GetWorld().Spawn<Prey>();
    Prey* second = app.GetWorld().Spawn<Prey>();
    hunter->prey.Set(first);
    EditorCommandRegistry commands;
    RegisterCommands(commands);
    husky::tools::PropertyEditRecorder recorder;
    const husky::core::Property& prey = *hunter->GetType().FindProperty("prey");

    // インスペクタが「prey」の箱を描き、2 つ目を選んだのと同じ順で呼ぶ(選ぶのはクリックなので、そのフレームで確定)
    recorder.BeginFrame(app, &commands);
    recorder.SetOwner(*hunter);
    recorder.PushPath(prey);
    recorder.BeforeLeaf(prey, hunter);
    hunter->prey.Set(second);
    recorder.AfterLeaf(prey, hunter, true, false);
    recorder.PopPath();
    recorder.EndFrame();
    CHECK(hunter->prey.Resolve(app.GetRegistry()) == second);
    CHECK(commands.GetUndoCount() == 1);

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(hunter->prey.Resolve(app.GetRegistry()) == first);
}

TEST_CASE("REF-1: シーンのインスペクタは参照を選ぶ箱で描いても ImGui の使い方の誤りが無く、消えた相手は (missing) と出す")
{
    husky::tests::HeadlessImGui imgui;
    Application app;
    Hunter* hunter = app.GetWorld().Spawn<Hunter>();
    Prey* prey = app.GetWorld().Spawn<Prey>();
    hunter->prey.Set(prey);
    prey->Destroy(); // 消えた相手(フレームの終わりまでは残る。インスペクタは消える予定のものを (missing) と出す)
    husky::tools::SceneInspector inspector;
    inspector.Select(hunter->GetHandle());
    imgui.Frame([&] { inspector.Draw(app, nullptr); });
    CHECK(hunter->prey.handle == prey->GetHandle()); // 描くだけでは変わらない
}

TEST_CASE("REF-1: 参照の配列も property.set で要素ごとに参照の形で書き換えられる。型の違う要素があれば何も変えない")
{
    Application app;
    Hunter* hunter = app.GetWorld().Spawn<Hunter>();
    hunter->name = "Hunter";
    Prey* a = app.GetWorld().Spawn<Prey>();
    a->name = "A";
    Prey* b = app.GetWorld().Spawn<Prey>();
    b->name = "B";
    EditorCommandRegistry commands;
    RegisterCommands(commands);

    REQUIRE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "flock", "value": ["A", null, "B"]})").succeeded);
    REQUIRE(hunter->flock.size() == 3);
    CHECK(hunter->flock[0].Resolve(app.GetRegistry()) == a);
    CHECK(hunter->flock[1].IsNull());
    CHECK(hunter->flock[2].Resolve(app.GetRegistry()) == b);

    // Hunter は Prey ではない → 何も変えずに断る
    CHECK_FALSE(SetProperty(commands, app, R"({"actor": "Hunter", "property": "flock", "value": ["B", "Hunter"]})").succeeded);
    CHECK(hunter->flock.size() == 3);

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded);
    CHECK(hunter->flock.empty());
}

TEST_CASE("REF-1: インスペクタで参照の配列の要素を選ぶ・足すと、配列まるごとの property.set の1件になり、Undo で戻る")
{
    Application app;
    Hunter* hunter = app.GetWorld().Spawn<Hunter>();
    Prey* a = app.GetWorld().Spawn<Prey>();
    Prey* b = app.GetWorld().Spawn<Prey>();
    hunter->flock.resize(1);
    hunter->flock[0].Set(a);
    EditorCommandRegistry commands;
    RegisterCommands(commands);
    husky::tools::PropertyEditRecorder recorder;
    const husky::core::Property& flock = *hunter->GetType().FindProperty("flock");
    const husky::core::ArrayInfo& array = *flock.GetArrayInfo();

    // 要素 0 を b に選び直す(インスペクタが配列の中で描くのと同じ順)
    recorder.BeginFrame(app, &commands);
    recorder.SetOwner(*hunter);
    recorder.PushPath(flock);
    recorder.EnterArray();
    void* element = array.at(flock.GetArrayAddressAt(hunter), 0);
    recorder.BeforeLeaf(array.element, element);
    hunter->flock[0].Set(b);
    recorder.AfterLeaf(array.element, element, true, false);
    // 「+」で1つ足す
    recorder.BeginWholeChange();
    array.resize(flock.GetArrayAddressAt(hunter), 2);
    recorder.CommitWholeChange();
    recorder.LeaveArray();
    recorder.PopPath();
    recorder.EndFrame();
    REQUIRE(hunter->flock.size() == 2);
    CHECK(hunter->flock[0].Resolve(app.GetRegistry()) == b);
    CHECK(commands.GetUndoCount() == 2);

    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded); // 足したのを戻す
    CHECK(hunter->flock.size() == 1);
    CHECK(hunter->flock[0].Resolve(app.GetRegistry()) == b);
    REQUIRE(commands.Execute("editor.undo", app, ArchiveValue()).succeeded); // 選び直したのを戻す
    CHECK(hunter->flock[0].Resolve(app.GetRegistry()) == a);
}
#endif
