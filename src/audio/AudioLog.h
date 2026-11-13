// 音の層(audio)のログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::audio
{
    enum class AudioLog : std::uint16_t
    {
        SdlCallFailed = 1,
        OutputStarted = 2,
        OutputStopped = 3,
        OutputUnavailable = 4,
        CommandQueueFull = 5,
    };

    HUSKY_LOG_CHANNEL_BEGIN(AudioLog, "Engine.Audio")
        HUSKY_LOG_MESSAGE(SdlCallFailed, Error, "{function} に失敗しました: {error}", HUSKY_LOG_ARG(function, String),
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(OutputStarted, Info, "音の出す先を開きました({driver}、{rate}Hz、{channels}ch)",
                          HUSKY_LOG_ARG(driver, String), HUSKY_LOG_ARG(rate, Int), HUSKY_LOG_ARG(channels, Int))
        HUSKY_LOG_MESSAGE(OutputStopped, Info, "音の出す先を閉じました(出したフレーム数 {frames})",
                          HUSKY_LOG_ARG(frames, Int))
        HUSKY_LOG_MESSAGE(OutputUnavailable, Warning, "音の出す先を開けませんでした。音なしで続けます")
        HUSKY_LOG_MESSAGE(CommandQueueFull, Warning, "音の命令の行列がいっぱいです。命令を捨てました(上限 {capacity})",
                          HUSKY_LOG_ARG(capacity, Int))
    HUSKY_LOG_CHANNEL_END()
}
