// 編集モード(EditorPlayMode)と、シーンの更新を止める口(Application::SetSceneTickEnabled)のテスト。
#include "engine/Application.h"

#include "engine/Actor.h"

#include <doctest/doctest.h>

using husky::engine::Actor;
using husky::engine::Application;

namespace
{
    // Tick のたびに右へ 1 ずつ進むアクター(更新が呼ばれたかを、位置で確かめる)。
    class Walker : public Actor
    {
        HUSKY_OBJECT(Walker, Actor)
    public:
        void Tick(double) override { transform.position.x += 1.0f; }
    };
}

TEST_CASE("Application: シーンの更新を止めている間は Tick を呼ばない。コマ送りを頼んだフレームだけ1回進む")
{
    Application app;
    app.GetClock().SetFixedDelta(0.01);
    Walker* walker = app.GetScene().Spawn<Walker>();
    REQUIRE(walker != nullptr);
    CHECK(app.IsSceneTickEnabled()); // 既定は動かす(製品とテスト)

    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 1.0f);

    app.SetSceneTickEnabled(false);
    app.StepFrame(0, {});
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 1.0f);

    app.RequestSceneStep();
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 2.0f);
    app.StepFrame(0, {}); // 頼んだのは1回だけ
    CHECK(walker->transform.position.x == 2.0f);

    app.SetSceneTickEnabled(true);
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 3.0f);
}

#if HUSKY_DEVELOPMENT

#include "tools/EditorPlayMode.h"

using husky::tools::EditorCommandRegistry;
using husky::tools::EditorPlayMode;

namespace
{
    // シーンにいる、生きている Walker を数え、最初の1体を返す。
    Walker* FindWalker(Application& app, std::size_t& count)
    {
        Walker* first = nullptr;
        count = 0;
        app.GetScene().ForEachActor([&](Actor& actor) {
            if (Walker* walker = husky::core::Cast<Walker>(&actor))
            {
                ++count;
                if (first == nullptr)
                {
                    first = walker;
                }
            }
        });
        return first;
    }
}

TEST_CASE("EditorPlayMode: 編集中は動かず、Play で動き、Stop で Play の前のシーンに戻る")
{
    Application app;
    app.GetTypeRegistry().Register<Walker>(); // 控えから作り直すときに、型の名前から作るため
    app.GetClock().SetFixedDelta(0.01);
    Walker* original = app.GetScene().Spawn<Walker>();
    REQUIRE(original != nullptr);
    original->transform.position.x = 10.0f;
    const husky::core::Guid originalGuid = original->guid;

    EditorPlayMode mode;
    EditorCommandRegistry commands;
    mode.RegisterCommands(commands);
    mode.EnterEditMode(app);
    CHECK(mode.GetState() == EditorPlayMode::State::Edit);

    app.StepFrame(0, {});
    CHECK(original->transform.position.x == 10.0f); // 編集中は動かない

    CHECK(commands.Execute("editor.play", app).succeeded);
    CHECK(mode.GetState() == EditorPlayMode::State::Playing);
    app.StepFrame(0, {});
    app.StepFrame(0, {});
    CHECK(original->transform.position.x == 12.0f);
    original->Destroy(); // 再生中に壊しても
    app.StepFrame(0, {});

    CHECK(commands.Execute("editor.stop", app).succeeded);
    CHECK(mode.GetState() == EditorPlayMode::State::Edit);
    app.StepFrame(0, {}); // フレームの終わりに、古いアクターが一覧から外れる

    std::size_t count = 0;
    Walker* restored = FindWalker(app, count);
    CHECK(count == 1); // 元の1体だけ(再生中のものと、控えから戻したものが両方いたりしない)
    REQUIRE(restored != nullptr);
    CHECK(restored->transform.position.x == 10.0f); // Play を押す前の位置
    CHECK(restored->guid == originalGuid); // 作り直しても、変わらない ID は同じ(コマンドが Stop の後も同じアクターを指せる)
    CHECK(app.GetScene().actors.size() == 1);

    app.StepFrame(0, {});
    CHECK(restored->transform.position.x == 10.0f); // 戻った後は、また編集中なので動かない
}

TEST_CASE("EditorPlayMode: 一時停止すると止まり、コマ送りで1フレームずつ進み、Play で続きから動く")
{
    Application app;
    app.GetTypeRegistry().Register<Walker>();
    app.GetClock().SetFixedDelta(0.01);
    Walker* walker = app.GetScene().Spawn<Walker>();
    EditorPlayMode mode;
    EditorCommandRegistry commands;
    mode.RegisterCommands(commands);
    mode.EnterEditMode(app);

    REQUIRE(commands.Execute("editor.play", app).succeeded);
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 1.0f);

    REQUIRE(commands.Execute("editor.pause", app).succeeded);
    CHECK(mode.GetState() == EditorPlayMode::State::Paused);
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 1.0f);

    REQUIRE(commands.Execute("editor.step", app).succeeded);
    app.StepFrame(0, {});
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 2.0f);

    REQUIRE(commands.Execute("editor.play", app).succeeded); // 続きから(控えは取り直さない)
    CHECK(mode.GetState() == EditorPlayMode::State::Playing);
    app.StepFrame(0, {});
    CHECK(walker->transform.position.x == 3.0f);

    REQUIRE(commands.Execute("editor.stop", app).succeeded);
    app.StepFrame(0, {});
    std::size_t count = 0;
    Walker* restored = FindWalker(app, count);
    REQUIRE(restored != nullptr);
    CHECK(restored->transform.position.x == 0.0f); // 最初の Play の前に戻る
}

TEST_CASE("EditorPlayMode: 今の状態で使えない操作は、理由つきで断る(シーンの保存は編集中だけ)")
{
    Application app;
    EditorPlayMode mode;
    EditorCommandRegistry commands;
    mode.RegisterCommands(commands);
    mode.EnterEditMode(app);

    // 編集中: 止める・一時停止・コマ送りはできない
    CHECK(mode.WhyCannotStop() != nullptr);
    CHECK(mode.WhyCannotPause() != nullptr);
    CHECK(mode.WhyCannotStep() != nullptr);
    CHECK(mode.WhyNotEditing() == nullptr);
    const husky::tools::EditorCommandResult refused = commands.Execute("editor.stop", app);
    CHECK_FALSE(refused.succeeded);
    CHECK(refused.message.find("not available") != std::string::npos);
    CHECK(mode.GetState() == EditorPlayMode::State::Edit);

    // 再生中: もう一度 Play はできない。編集中だけの操作もできない
    REQUIRE(commands.Execute("editor.play", app).succeeded);
    CHECK_FALSE(commands.Execute("editor.play", app).succeeded);
    CHECK(mode.WhyNotEditing() != nullptr);

    husky::tools::EditorCommand save;
    save.name = "test.save";
    save.label = "Save";
    save.execute = [](husky::tools::EditorCommandContext&) { return husky::tools::EditorCommandResult::Success("saved"); };
    save.whyUnavailable = [&mode](Application&) { return mode.WhyNotEditing(); };
    REQUIRE(commands.Register(save));
    CHECK_FALSE(commands.Execute("test.save", app).succeeded);
    REQUIRE(commands.Execute("editor.stop", app).succeeded);
    CHECK(commands.Execute("test.save", app).succeeded);
}

#endif

#if HUSKY_DEVELOPMENT // editor のコマンドは開発ビルドだけ
#include "ManualAudioOutput.h"

#include <memory>

TEST_CASE("EditorPlayMode: 一時停止すると音も止まり、Play で続きから鳴る。Stop ですべて止まる")
{
    Application app;
    husky::tests::ManualOutputState state;
    REQUIRE(app.GetAudio().StartOutput(std::make_unique<husky::tests::ManualOutput>(state, true)));
    EditorPlayMode mode;
    EditorCommandRegistry commands;
    mode.RegisterCommands(commands);
    mode.EnterEditMode(app);
    REQUIRE(commands.Execute("editor.play", app).succeeded);

    const std::vector<float> samples(100 * husky::audio::OutputChannels, 0.5f);
    const husky::audio::PlaybackId id =
        app.GetAudio().Play(husky::audio::SoundData{samples.data(), 100}, husky::audio::PlayParams{});
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));

    REQUIRE(commands.Execute("editor.pause", app).succeeded);
    CHECK(state.Render(1)[0] == 0.0f);
    CHECK(app.GetAudio().IsPlaying(id));

    REQUIRE(commands.Execute("editor.play", app).succeeded); // 続きから
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));

    REQUIRE(commands.Execute("editor.pause", app).succeeded);
    REQUIRE(commands.Execute("editor.stop", app).succeeded); // 一時停止したまま止めても、次は鳴らせる
    CHECK_FALSE(app.GetAudio().IsPlaying(id));
    app.GetAudio().Play(husky::audio::SoundData{samples.data(), 100}, husky::audio::PlayParams{});
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));
    app.GetAudio().StopOutput();
}
#endif
