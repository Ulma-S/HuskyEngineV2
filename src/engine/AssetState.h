// アセットの読み込みの状態。どの種類のアセット(テクスチャ、スプライトシートなど)でも共通に使う。
//
// 動作スレッド: どこでもよい(ただの値)
#pragma once

#include <cstdint>

namespace husky::engine
{
    enum class AssetState : std::uint8_t
    {
        Loading, // 読み込み中(ワーカーが読んでいる。終わると AssetLoader::Update が Loaded か Failed にする)
        Loaded,  // 読み込めた
        Failed,  // 読み込めなかった(GUID が表に無い、ファイルが壊れているなど。理由はログに出る)
    };
}
