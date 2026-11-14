// アクターの位置・回転・拡大縮小(Transform)。どのアクターも必ず1つ持つ(Actor::transform)。
// Unity の Transform と同じ役目だが、付け外しできるコンポーネントではなく、アクターのメンバーにしている
// (どのアクターも持つので、毎回 GetComponent で探すより簡単で速い。2026-11-13 にユーザーと決定)。
//
// 決まり(設計メモ「座標系とカメラ」):
//   - ワールドの単位。右が +x、上が +y。z は奥行き(2D では今は使わない。描く順に使う余地を残す)
//   - 回転は z 軸まわりの角度1つ(度。反時計回りが +)。3D に入ったらクォータニオンに替える
//   - 拡大縮小は x・y・z(1 で元の大きさ)
//   - 親子の階層(HIER-1): 値は親から見たローカルの値(Unity の localPosition など)。親が無ければワールドの値と同じ。
//     ワールドの値は Actor::GetWorldTransform などで計算する
// 値は自前の型(core::Vector3)で持ち、glm には依存しない。行列などの計算をするときは ToGlm で変える(GlmConversion.h)。
// 動作スレッド: ゲームスレッドのみ(アクターのメンバーとして)
#pragma once

#include "core/StructReflection.h"
#include "core/Vector3.h"

namespace husky::engine
{
    struct Transform
    {
        core::Vector3 position;                   // 親から見た位置(親が無ければワールドでの位置。既定は原点)
        float rotation = 0.0f;                    // z 軸まわりの角度(度。反時計回りが +)
        core::Vector3 scale = {1.0f, 1.0f, 1.0f}; // 拡大縮小(1 で元の大きさ)
    };

    // 入れ子の構造体(core::Vector3)は、core/Vector3.h で先に登録してある。
    HUSKY_STRUCT_DECLARE(Transform) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)
}
