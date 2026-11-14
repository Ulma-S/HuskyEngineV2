// インスペクタの書き換えを property.set の1件にまとめる記録係(PropertyEditRecorder)のテスト(開発ビルドだけ)。
// ImGui を使わず、「部品が値を変えた」「まだ操作中か」をテストから渡して確かめる。
#if HUSKY_DEVELOPMENT

#include "tools/PropertyEditRecorder.h"

#include "engine/Actor.h"
#include "engine/SpriteComponent.h"
#include "tools/EditorCommands.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::Property;
using husky::engine::Actor;
using husky::engine::Application;
using husky::tools::EditorCommandRegistry;
using husky::tools::PropertyEditRecorder;

namespace
{
    // 1フレーム分: actor の transform.position.x の部品を描いたことにする。
    // edit: 部品の中で値を書き換える処理(部品が値を変えたことにする)。active: 描いた後も操作中か。
    template <typename Edit>
    void DrawPositionX(PropertyEditRecorder& recorder, Application& app, EditorCommandRegistry& commands, Actor& actor,
                       Edit&& edit, bool changed, bool active)
    {
        const Property& transform = *actor.GetType().FindProperty("transform");
        const Property& position = *transform.GetStructType()->FindProperty("position");
        const Property& x = *position.GetStructType()->FindProperty("x");
        void* transformAddress = transform.GetStructAddressAt(&actor);
        void* positionAddress = position.GetStructAddressAt(transformAddress);

        recorder.BeginFrame(app, &commands);
        recorder.SetOwner(actor);
        recorder.PushPath(transform);
        recorder.BeforeLeaf(transform, &actor);
        recorder.PushPath(position);
        recorder.BeforeLeaf(position, transformAddress);
        recorder.PushPath(x);
        recorder.BeforeLeaf(x, positionAddress);
        edit();
        recorder.AfterLeaf(x, positionAddress, changed, active);
        recorder.PopPath();
        recorder.AfterLeaf(position, transformAddress, false, false); // 構造体の見出し(値1つではないので記録しない)
        recorder.PopPath();
        recorder.AfterLeaf(transform, &actor, false, false);
        recorder.PopPath();
        recorder.EndFrame();
    }

    void RegisterHistoryCommands(EditorCommandRegistry& commands, const bool& editing)
    {
        husky::tools::EditorCommand setProperty = husky::tools::MakeSetPropertyCommand();
        setProperty.whyUnavailable = [&editing](Application&) { return editing ? nullptr : "Stop playing first"; };
        REQUIRE(commands.Register(std::move(setProperty)));
        REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
        REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));
    }
}

TEST_CASE("PropertyEditRecorder: ドラッグ中の連続した変更は、手を離したときに property.set の1件になり、Undo で前の値に戻る")
{
    Application app;
    Actor* actor = app.GetWorld().Spawn<Actor>();
    actor->name = "Player";
    bool editing = true;
    EditorCommandRegistry commands;
    RegisterHistoryCommands(commands, editing);
    PropertyEditRecorder recorder;
    float& x = actor->transform.position.x;

    // 3フレームのあいだドラッグする(値はその場で変わるが、まだ履歴には入らない)
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 1.0f; }, true, true);
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 2.0f; }, true, true);
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 3.0f; }, true, true);
    CHECK(recorder.IsEditing());
    CHECK(x == 3.0f);
    CHECK(commands.GetUndoCount() == 0);

    // 手を離す: 1件の property.set になる(値は最後の値のまま)
    DrawPositionX(recorder, app, commands, *actor, [] {}, false, false);
    CHECK_FALSE(recorder.IsEditing());
    CHECK(x == 3.0f);
    REQUIRE(commands.GetUndoCount() == 1);
    CHECK(std::string(commands.PeekUndoLabel()) == "Set Player (Actor).transform.position.x = 3.0");

    // 1回の Undo で、ドラッグを始める前の値に戻る
    CHECK(commands.Execute("editor.undo", app).succeeded);
    CHECK(x == 0.0f);
    CHECK(commands.Execute("editor.redo", app).succeeded);
    CHECK(x == 3.0f);

    // ドラッグして元の値に戻しただけなら、履歴に積まない
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 4.0f; }, true, true);
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 3.0f; }, true, false);
    CHECK(commands.GetUndoCount() == 1);
}

TEST_CASE("PropertyEditRecorder: 一度で変わる部品(色やチェックボックス)は、そのフレームで1件になる。コンポーネントは型の名前で指す")
{
    Application app;
    Actor* actor = app.GetWorld().Spawn<Actor>();
    actor->name = "Player";
    auto* sprite = actor->AddComponent<husky::engine::SpriteComponent>();
    bool editing = true;
    EditorCommandRegistry commands;
    RegisterHistoryCommands(commands, editing);
    PropertyEditRecorder recorder;

    const Property& color = *sprite->GetType().FindProperty("color");
    recorder.BeginFrame(app, &commands);
    recorder.SetOwner(*sprite);
    recorder.PushPath(color);
    recorder.BeforeLeaf(color, sprite);
    sprite->color.g = 0.25f;
    recorder.AfterLeaf(color, sprite, true, false);
    recorder.PopPath();
    recorder.EndFrame();

    REQUIRE(commands.GetUndoCount() == 1);
    CHECK(sprite->color.g == 0.25f);
    CHECK(commands.Execute("editor.undo", app).succeeded);
    CHECK(sprite->color.g == 1.0f);
    CHECK(sprite->color.r == 1.0f);
}

TEST_CASE("PropertyEditRecorder: 編集の途中の部品が描かれなくなったら確定する。再生中(property.set が使えない)は履歴に残さない")
{
    Application app;
    Actor* actor = app.GetWorld().Spawn<Actor>();
    bool editing = true;
    EditorCommandRegistry commands;
    RegisterHistoryCommands(commands, editing);
    PropertyEditRecorder recorder;
    float& x = actor->transform.position.x;

    // ドラッグの途中で、部品が描かれなくなった(選択を変えたなど)→ そのフレームの終わりに確定する
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 5.0f; }, true, true);
    recorder.BeginFrame(app, &commands);
    recorder.EndFrame();
    CHECK_FALSE(recorder.IsEditing());
    CHECK(commands.GetUndoCount() == 1);

    // 再生中: 値はその場で変わったまま、履歴には入らない
    editing = false;
    DrawPositionX(recorder, app, commands, *actor, [&] { x = 9.0f; }, true, false);
    CHECK(x == 9.0f);
    CHECK(commands.GetUndoCount() == 1);

    // commands が無ければ、値が変わるだけ(テストや、コマンドを使わない画面)
    editing = true;
    recorder.BeginFrame(app, nullptr);
    recorder.EndFrame();
    PropertyEditRecorder plain;
    const Property& name = *actor->GetType().FindProperty("name");
    plain.BeginFrame(app, nullptr);
    plain.SetOwner(*actor);
    plain.PushPath(name);
    plain.BeforeLeaf(name, actor);
    actor->name = "Changed";
    plain.AfterLeaf(name, actor, true, false);
    plain.PopPath();
    plain.EndFrame();
    CHECK(actor->name == "Changed");
    CHECK(commands.GetUndoCount() == 1);
}

#endif
