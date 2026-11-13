// 跳ね回るものの設定(SD-4)。ゲームが作る ScriptableData(Unity の ScriptableObject にあたる)の最初の例。
// ファイルは assets/data/bouncers.sdata。値はアセットの窓(Assets → Open)か、data.set で直す(コードを直さずに調整できる)。
//
// 2つの使い方をしている:
//   - 置くときの設定(count、maxSpeed、prefab): SpawnInitialActors がシーンのファイルの無いときに読む
//   - 動いている間の設定(touchedColor): 置いた BouncerActor がプロパティの AssetRef で指し、触れたときに読む
//     (1000 体が同じ1つのデータを指す。1体ずつ値を持たないので、直すと全員が変わる)
//
// 読み込んだデータは読むだけ(ローダは const のポインタで返し、書き込むと SD-2 の検知でエラーになる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/AssetRef.h"
#include "engine/PrefabAsset.h"
#include "engine/RenderReflection.h" // render::Color をプロパティにする
#include "engine/ScriptableData.h"

#include <cstdint>

namespace husky::game
{
    class BouncerSettings : public engine::ScriptableData
    {
        HUSKY_OBJECT(BouncerSettings, engine::ScriptableData)
    public:
        std::int32_t count = 1000;    // 置く数
        float maxSpeed = 200.0f;      // 速さの上限(x と y を、それぞれ -maxSpeed〜+maxSpeed から乱数で決める。ピクセル/秒)
        render::Color touchedColor = {1.0f, 0.25f, 0.25f, 1.0f}; // プレイヤーに触れている間の色
        core::AssetRef<engine::PrefabAsset> prefab; // 置くプレハブ(何も指さなければ prefabs/bouncer.prefab を探す)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(count)
            HUSKY_PROPERTY(maxSpeed)
            HUSKY_PROPERTY(touchedColor)
            HUSKY_PROPERTY(prefab)
        HUSKY_REFLECTION_END()
    };
}
