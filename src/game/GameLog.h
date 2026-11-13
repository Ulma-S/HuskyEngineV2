// ゲームのログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::game
{
    enum class GameLog : std::uint16_t
    {
        PrefabNotInDatabase = 1,
        PrefabLoadFailed = 2,
        PrefabWarning = 3,
        SettingsNotInDatabase = 4,
        SettingsLoadFailed = 5,
        BouncerSpawnFailed = 6,
        AssetsDirectory = 7,
        AssetScan = 8,
        NoSceneFile = 9,
        ClearColorChanged = 10,
    };

    HUSKY_LOG_CHANNEL_BEGIN(GameLog, "Game")
        HUSKY_LOG_MESSAGE(PrefabNotInDatabase, Error, "プレハブがアセットの表にありません({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(PrefabLoadFailed, Error, "プレハブを読めませんでした({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(PrefabWarning, Warning, "プレハブの注意: {warning}", HUSKY_LOG_ARG(warning, String))
        HUSKY_LOG_MESSAGE(SettingsNotInDatabase, Warning, "設定のデータがアセットの表にありません({path})。既定の値を使います",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(SettingsLoadFailed, Warning, "設定のデータを読めませんでした({path})。既定の値を使います",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(BouncerSpawnFailed, Error, "跳ね回るアクターを置けませんでした({index} 個目)",
                          HUSKY_LOG_ARG(index, Int))
        HUSKY_LOG_MESSAGE(AssetsDirectory, Info, "アセットのフォルダ: {path}", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(AssetScan, Info,
                          "アセット: {assets} 個(.meta が無い {missingMeta}、読めない .meta {invalidMeta}、対の無い .meta {orphanMeta}、GUID の重なり {duplicateGuid})",
                          HUSKY_LOG_ARG(assets, Int), HUSKY_LOG_ARG(missingMeta, Int), HUSKY_LOG_ARG(invalidMeta, Int),
                          HUSKY_LOG_ARG(orphanMeta, Int), HUSKY_LOG_ARG(duplicateGuid, Int))
        HUSKY_LOG_MESSAGE(NoSceneFile, Info, "シーンのファイルが無いので、コードでアクターを置きます({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(ClearColorChanged, Info, "色を切り替えました: {index}", HUSKY_LOG_ARG(index, Int))
    HUSKY_LOG_CHANNEL_END()
}
