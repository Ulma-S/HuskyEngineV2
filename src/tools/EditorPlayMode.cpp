// EditorPlayMode.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorPlayMode.h"

#if HUSKY_DEVELOPMENT

#include "engine/Actor.h"
#include "engine/SceneSerializer.h"

#include <cstdio>  // std::printf
#include <string>
#include <utility> // std::move

namespace husky::tools
{
    const char* EditorPlayMode::GetStateName(State state)
    {
        switch (state)
        {
        case State::Edit: return "EDIT";
        case State::Playing: return "PLAYING";
        case State::Paused: return "PAUSED";
        }
        return "";
    }

    void EditorPlayMode::EnterEditMode(engine::Application& app)
    {
        app.SetSceneTickEnabled(false);
        m_state = State::Edit;
        m_snapshot = engine::ArchiveValue();
    }

    const char* EditorPlayMode::WhyCannotPlay() const
    {
        return m_state == State::Playing ? "Already playing" : nullptr;
    }

    const char* EditorPlayMode::WhyCannotPause() const
    {
        return m_state == State::Playing ? nullptr : "Not playing";
    }

    const char* EditorPlayMode::WhyCannotStep() const
    {
        return m_state == State::Paused ? nullptr : "Pause first";
    }

    const char* EditorPlayMode::WhyCannotStop() const
    {
        return m_state == State::Edit ? "Not playing" : nullptr;
    }

    const char* EditorPlayMode::WhyNotEditing() const
    {
        return m_state == State::Edit ? nullptr : "Stop playing first (changes made while playing are not saved)";
    }

    EditorCommandResult EditorPlayMode::Play(engine::Application& app)
    {
        if (const char* reason = WhyCannotPlay())
        {
            return EditorCommandResult::Failure(reason);
        }
        if (m_state == State::Paused) // 一時停止から: 控えはそのまま、続きから動かす
        {
            m_state = State::Playing;
            app.SetSceneTickEnabled(true);
            return EditorCommandResult::Success("Resumed");
        }

        // 編集から: 今のシーンを控えてから動かす。控えは保存と同じ形の値の木(ファイルには書かない)。
        engine::SceneSerializer::WriteResult saved = engine::SceneSerializer::Write(app.GetScene());
        m_snapshot = std::move(saved.value);
        m_state = State::Playing;
        app.SetSceneTickEnabled(true);
        EditorCommandResult result = EditorCommandResult::Success(
            "Playing (" + std::to_string(app.GetScene().actors.size()) + " actors saved for Stop)");
        result.details = std::move(saved.warnings); // 控えに入らなかったもの(保存の警告と同じ)
        return result;
    }

    EditorCommandResult EditorPlayMode::Pause(engine::Application& app)
    {
        if (const char* reason = WhyCannotPause())
        {
            return EditorCommandResult::Failure(reason);
        }
        m_state = State::Paused;
        app.SetSceneTickEnabled(false);
        return EditorCommandResult::Success("Paused");
    }

    EditorCommandResult EditorPlayMode::Step(engine::Application& app)
    {
        if (const char* reason = WhyCannotStep())
        {
            return EditorCommandResult::Failure(reason);
        }
        app.RequestSceneStep(); // 次の StepFrame で1回だけ Tick する
        return EditorCommandResult::Success("Stepped one frame");
    }

    EditorCommandResult EditorPlayMode::Stop(engine::Application& app)
    {
        if (const char* reason = WhyCannotStop())
        {
            return EditorCommandResult::Failure(reason);
        }

        // 1. 今いるアクターをすべて Destroy する(消えるのはフレームの終わり。それまでは Tick も描画もされない)。
        engine::Scene& scene = app.GetScene();
        core::ObjectRegistry& registry = app.GetRegistry();
        for (const core::ObjectHandle handle : scene.actors)
        {
            if (engine::Actor* actor = registry.Resolve<engine::Actor>(handle))
            {
                actor->Destroy();
            }
        }

        // 2. 控えから、再生を始める前のアクターを作り直す(一覧の後ろに足される。消す印の付いた古いものは、フレームの終わりに外れる)。
        const engine::SceneSerializer::ReadReport report =
            engine::SceneSerializer::Read(m_snapshot, scene, app.GetTypeRegistry(),
                                          [&app](const core::Guid& guid) { return app.FindPrefabForScene(guid); });

        // 3. 編集の状態に戻す。
        EnterEditMode(app);

        EditorCommandResult result = EditorCommandResult::Success(
            "Stopped (restored " + std::to_string(report.addedActorCount) + " actors)");
        result.details = report.warnings;
        return result;
    }

    void EditorPlayMode::RegisterCommands(EditorCommandRegistry& commands)
    {
        // ラムダ式は this(この EditorPlayMode)を覚える。[this] は C# のラムダ式が自分のインスタンスを捕まえるのと同じ。
        EditorCommand play;
        play.name = "editor.play";
        play.label = "Play";
        play.description = "Start playing the scene (the current scene is kept in memory and comes back on Stop), "
                           "or resume from pause.";
        play.group = "play";
        play.execute = [this](EditorCommandContext& context) { return Play(context.app); };
        play.whyUnavailable = [this](engine::Application&) { return WhyCannotPlay(); };
        commands.Register(std::move(play));

        EditorCommand pause;
        pause.name = "editor.pause";
        pause.label = "Pause";
        pause.description = "Pause the scene while playing.";
        pause.group = "play";
        pause.execute = [this](EditorCommandContext& context) { return Pause(context.app); };
        pause.whyUnavailable = [this](engine::Application&) { return WhyCannotPause(); };
        commands.Register(std::move(pause));

        EditorCommand step;
        step.name = "editor.step";
        step.label = "Step";
        step.description = "While paused, advance the scene by one frame.";
        step.group = "play";
        step.execute = [this](EditorCommandContext& context) { return Step(context.app); };
        step.whyUnavailable = [this](engine::Application&) { return WhyCannotStep(); };
        commands.Register(std::move(step));

        EditorCommand stop;
        stop.name = "editor.stop";
        stop.label = "Stop";
        stop.description = "Stop playing and go back to the scene as it was before Play (changes made while playing are lost).";
        stop.group = "play";
        stop.execute = [this](EditorCommandContext& context) { return Stop(context.app); };
        stop.whyUnavailable = [this](engine::Application&) { return WhyCannotStop(); };
        commands.Register(std::move(stop));
    }
}

#endif
