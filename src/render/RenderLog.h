// 描画層(render)のログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::render
{
    enum class RenderLog : std::uint16_t
    {
        SdlCallFailed = 1,
        NoShaderFormat = 2,
        DevUiInitFailed = 3,
        DevUiUnavailable = 4,
        AlreadyInitialized = 5,
        WindowNotOpen = 6,
        GpuDriver = 7,
        InvalidTextureSize = 8,
        NotInitialized = 9,
        SpriteBufferFailed = 10,
        DevUiFontFailed = 11,
        TooManyViewProjections = 12,
        InvalidTextureRegion = 13,
        TextureNotAlive = 14,
    };

    HUSKY_LOG_CHANNEL_BEGIN(RenderLog, "Engine.Render")
        HUSKY_LOG_MESSAGE(SdlCallFailed, Error, "{function} に失敗しました: {error}", HUSKY_LOG_ARG(function, String),
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(NoShaderFormat, Error, "使えるシェーダの形式がありません")
        HUSKY_LOG_MESSAGE(DevUiInitFailed, Error, "{function} に失敗しました", HUSKY_LOG_ARG(function, String))
        HUSKY_LOG_MESSAGE(DevUiUnavailable, Warning, "開発用の UI(ImGui)を用意できませんでした。UI なしで続けます")
        HUSKY_LOG_MESSAGE(AlreadyInitialized, Warning, "RenderDevice はすでに初期化されています")
        HUSKY_LOG_MESSAGE(WindowNotOpen, Error, "RenderDevice::Initialize: ウィンドウが開いていません")
        HUSKY_LOG_MESSAGE(GpuDriver, Info, "GPU の API: {driver}", HUSKY_LOG_ARG(driver, String))
        HUSKY_LOG_MESSAGE(InvalidTextureSize, Error, "CreateTexture: 画像の大きさが不正です({width} × {height})",
                          HUSKY_LOG_ARG(width, Int), HUSKY_LOG_ARG(height, Int))
        HUSKY_LOG_MESSAGE(NotInitialized, Error, "{function}: RenderDevice がまだ初期化されていません",
                          HUSKY_LOG_ARG(function, String))
        HUSKY_LOG_MESSAGE(SpriteBufferFailed, Error, "スプライト用のバッファを作れませんでした: {error}",
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(DevUiFontFailed, Warning, "開発用の UI のフォントを読めませんでした。既定のフォント(英数字だけ)で続けます")
        HUSKY_LOG_MESSAGE(TooManyViewProjections, Warning,
                          "SetViewProjection: 1フレームに使える行列は {max} 個までです。それより後は前の行列のまま描きます",
                          HUSKY_LOG_ARG(max, Int))
        HUSKY_LOG_MESSAGE(InvalidTextureRegion, Error,
                          "UpdateTexture: 書き換える四角 ({x}, {y}, {width} × {height}) がテクスチャ ({textureWidth} × {textureHeight}) に収まらないか、画像の大きさが合いません",
                          HUSKY_LOG_ARG(x, Int), HUSKY_LOG_ARG(y, Int), HUSKY_LOG_ARG(width, Int), HUSKY_LOG_ARG(height, Int),
                          HUSKY_LOG_ARG(textureWidth, Int), HUSKY_LOG_ARG(textureHeight, Int))
        HUSKY_LOG_MESSAGE(TextureNotAlive, Error, "{function}: 手放されたか、何も指さないテクスチャです",
                          HUSKY_LOG_ARG(function, String))
    HUSKY_LOG_CHANNEL_END()
}
