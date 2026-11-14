// GameModuleCommands.h の実装。
#include "tools/GameModuleCommands.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/GameModule.h"
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
}

#endif
