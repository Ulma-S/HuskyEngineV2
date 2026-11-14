// platform のログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::platform
{
    enum class PlatformLog : std::uint16_t
    {
        SdlCallFailed = 1,
        FileReadFailed = 2,
        FileWriteFailed = 3,
        FileReplaceFailed = 4,
        DirectoryCreateFailed = 5,
        DirectoryReadFailed = 6,
        ImageLoadFailed = 7,
        MemoryHooksMissing = 8,
        WindowAlreadyOpen = 9,
        LibraryLoadFailed = 10,
        LibraryFunctionMissing = 11,
        FileCopyFailed = 12,
    };

    HUSKY_LOG_CHANNEL_BEGIN(PlatformLog, "Engine.Platform")
        HUSKY_LOG_MESSAGE(SdlCallFailed, Error, "{function} に失敗しました: {error}", HUSKY_LOG_ARG(function, String),
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(FileReadFailed, Error, "ファイルを読めませんでした({path}): {error}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(FileWriteFailed, Error, "ファイルを書けませんでした({path}): {error}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(FileReplaceFailed, Error, "ファイルを置き換えられませんでした({from} → {to}): {error}",
                          HUSKY_LOG_ARG(from, String), HUSKY_LOG_ARG(to, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(DirectoryCreateFailed, Error, "フォルダを作れませんでした({path}): {error}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(DirectoryReadFailed, Error, "フォルダを読めませんでした({path}): {error}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(ImageLoadFailed, Error, "画像を読めませんでした({path}): {error}", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(MemoryHooksMissing, Warning,
                          "InstallMemoryHooks が呼ばれていません。SDL のメモリは集計に入りません")
        HUSKY_LOG_MESSAGE(WindowAlreadyOpen, Warning, "ウィンドウはすでに開いています")
        HUSKY_LOG_MESSAGE(LibraryLoadFailed, Error, "ライブラリを読めませんでした({path}): {error}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(LibraryFunctionMissing, Error, "ライブラリ({path})に関数 {name} がありません",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(FileCopyFailed, Error, "ファイルを写せませんでした({from} → {to}): {error}",
                          HUSKY_LOG_ARG(from, String), HUSKY_LOG_ARG(to, String), HUSKY_LOG_ARG(error, String))
    HUSKY_LOG_CHANNEL_END()
}
