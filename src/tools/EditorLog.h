// エディタ(tools)のログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::tools
{
    enum class EditorLog : std::uint16_t
    {
        CommandSucceeded = 1,
        CommandFailed = 2,
        CommandDetail = 3,
        DataCopyFailed = 4,
        AutoReloadStarted = 5,
        AutoReloadWaiting = 6,
    };

    HUSKY_LOG_CHANNEL_BEGIN(EditorLog, "Editor")
        HUSKY_LOG_MESSAGE(CommandSucceeded, Info, "コマンド {command}: 成功 {message}", HUSKY_LOG_ARG(command, String),
                          HUSKY_LOG_ARG(message, String))
        HUSKY_LOG_MESSAGE(CommandFailed, Warning, "コマンド {command}: 失敗 {message}", HUSKY_LOG_ARG(command, String),
                          HUSKY_LOG_ARG(message, String))
        // コマンドの結果の細かい内容(1行ずつ)。
        HUSKY_LOG_MESSAGE(CommandDetail, Info, "  {detail}", HUSKY_LOG_ARG(detail, String))
        HUSKY_LOG_MESSAGE(DataCopyFailed, Error, "アセットの窓: {type} の写しを作れません(型が登録されていないか、メモリが足りません)",
                          HUSKY_LOG_ARG(type, String))
        // ゲームの DLL の見張り(HR-5。tools/GameModuleCommands.h の GameModuleAutoReload)
        HUSKY_LOG_MESSAGE(AutoReloadStarted, Info, "ゲームの DLL がビルドし直されたので、読み直します({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(AutoReloadWaiting, Info, "ゲームの DLL がビルドし直されました。再生中なので、Stop した後に読み直します({path})",
                          HUSKY_LOG_ARG(path, String))
    HUSKY_LOG_CHANNEL_END()
}
