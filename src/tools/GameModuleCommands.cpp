// GameModuleCommands.h の実装。
#include "tools/GameModuleCommands.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/GameModule.h"
#include "tools/EditorLog.h"
#include "tools/EditorPlayMode.h"

namespace husky::tools
{
    EditorCommand MakeGameReloadCommand(const EditorPlayMode& playMode)
    {
        EditorCommand command;
        command.name = "game.reload";
        command.label = "Reload game code";
        command.description =
            "Reload the rebuilt game library (HuskyGame) without closing the editor (while editing). "
            "The scene is saved to memory, all actors are destroyed, the old library is unloaded, the new one is loaded, "
            "and the scene is read back. Values that are not saved (timers and so on) are reset. "
            "Rebuild only the game library first (for example: cmake --build --preset debug --target husky_game_dll).";
        // 再生中は控えが2つになり、Stop で戻す控えが古い型を指すので、まずは編集中だけ(ユーザーの決定 3A)
        command.whyUnavailable = [&playMode](engine::Application& app) -> const char* {
            if (const char* reason = playMode.WhyNotEditing())
            {
                return reason;
            }
            if (!app.GetGameModule().IsFromLibrary())
            {
                return "The game code is statically linked (no library to reload)";
            }
            return nullptr;
        };
        command.execute = [](EditorCommandContext& context) {
            engine::GameModuleReloadResult reloaded = engine::ReloadGameModule(context.app);
            EditorCommandResult result = reloaded.succeeded ? EditorCommandResult::Success(reloaded.message)
                                                            : EditorCommandResult::Failure(reloaded.message);
            result.details = std::move(reloaded.warnings);
            return result;
        };
        return command;
    }

    void DllChangeDetector::Reset(std::int64_t loadedTime)
    {
        m_loadedTime = loadedTime;
        m_hasPending = false;
        m_announced = false;
    }

    bool DllChangeDetector::IsCheckDue(double now)
    {
        if (now < m_nextCheck)
        {
            return false;
        }
        m_nextCheck = now + CheckIntervalSeconds;
        return true;
    }

    DllChangeDetector::Action DllChangeDetector::Update(double now, bool readOk, std::int64_t time, bool editing)
    {
        if (!readOk || time == m_loadedTime)
        {
            // 変わっていない(か、書いている途中で読めない)。待っていた変化も取り消す
            m_hasPending = false;
            return Action::None;
        }
        if (!m_hasPending || time != m_pendingTime)
        {
            // 初めて見た新しい日時。落ち着くのを待つ(リンカはファイルを何回かに分けて書くことがある)
            m_hasPending = true;
            m_pendingTime = time;
            m_pendingSince = now;
            return Action::None;
        }
        if (now - m_pendingSince < SettleSeconds)
        {
            return Action::None;
        }
        if (!editing)
        {
            if (!m_announced)
            {
                m_announced = true;
                return Action::WaitForEdit;
            }
            return Action::None;
        }
        // 読み直す。失敗しても同じ日時では繰り返さない(壊れたビルドで、0.5 秒ごとに失敗し続けないため)
        Reset(time);
        return Action::Reload;
    }

    void GameModuleAutoReload::RegisterCommands(EditorCommandRegistry& commands)
    {
        EditorCommand command;
        command.name = "game.autoReload";
        command.label = "Toggle auto reload";
        command.description =
            "Turn on or off reloading the game library automatically when it is rebuilt (on by default). "
            "The library file is checked every 0.5 seconds. While playing, the reload waits until Stop.";
        command.whyUnavailable = [](engine::Application& app) -> const char* {
            return app.GetGameModule().IsFromLibrary() ? nullptr : "The game code is statically linked (no library to reload)";
        };
        command.execute = [this](EditorCommandContext&) {
            m_enabled = !m_enabled;
            m_loadedPath.clear(); // オンにしたときに、今のファイルの日時から見張り直す
            return EditorCommandResult::Success(m_enabled ? "Auto reload: on" : "Auto reload: off");
        };
        commands.Register(std::move(command));
    }

    void GameModuleAutoReload::Update(engine::Application& app, EditorCommandRegistry& commands,
                                      const EditorPlayMode& playMode)
    {
        engine::GameModuleHost& host = app.GetGameModule();
        if (!m_enabled || !host.IsFromLibrary())
        {
            return;
        }
        // 読んだ・読み直した(ボタンでも)ら、元のファイルの今の日時を「読んでいるもの」として覚え直す。
        // 文字列の比べは確保しない。代入(確保)は読み直したときだけ。
        if (host.GetLoadedPath() != m_loadedPath)
        {
            m_loadedPath = host.GetLoadedPath();
            if (m_file.GetPath() != host.GetSourcePath())
            {
                m_file.SetPath(host.GetSourcePath());
            }
            std::int64_t time = 0;
            m_file.Read(time);
            m_detector.Reset(time);
        }

        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        if (!m_detector.IsCheckDue(now))
        {
            return;
        }
        std::int64_t time = 0;
        const bool readOk = m_file.Read(time);
        switch (m_detector.Update(now, readOk, time, playMode.WhyNotEditing() == nullptr))
        {
        case DllChangeDetector::Action::Reload:
            HUSKY_LOG(EditorLog::AutoReloadStarted, host.GetSourcePath());
            commands.Execute("game.reload", app); // 結果はコマンドの仕組みがログに出す
            break;
        case DllChangeDetector::Action::WaitForEdit:
            HUSKY_LOG(EditorLog::AutoReloadWaiting, host.GetSourcePath());
            break;
        case DllChangeDetector::Action::None:
            break;
        }
    }
}

#endif
