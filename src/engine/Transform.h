// アクターの位置・回転・拡大縮小(Transform)。どのアクターも必ず1つ持つ(Actor::transform)。
// Unity の Transform と同じ役目だが、付け外しできるコンポーネントではなく、アクターのメンバーにしている
// (どのアクターも持つので、毎回 GetComponent で探すより簡単で速い。2026-11-13 にユーザーと決定)。
//
// 決まり(設計メモ「座標系とカメラ」):
//   - ワールドの単位。右が +x、上が +y。z は奥行き(2D では今は使わない。描く順に使う余地を残す)
//   - 回転は z 軸まわりの角度1つ(度。反時計回りが +)。3D に入ったらクォータニオンに替える
//   - 拡大縮小は x・y・z(1 で元の大きさ)
//   - 親子の階層は、今は作らない(位置はいつもワールドでの位置)
// 回転と拡大縮小は、今は値を持って保存するだけで、描画にはまだ効かない(「回転と拡大縮小」のステップで効かせる)。
// 動作スレッド: ゲームスレッドのみ(アクターのメンバーとして)
#pragma once

#include "core/StructReflection.h"

#include <glm/vec3.hpp>

// glm::vec3 を、リフレクション対応の構造体として登録する(x、y、z の3つの float)。
// 登録は構造体と同じ名前空間に書く決まり(関数を ADL で見つけるため)なので、glm の名前空間の中に書く。
// glm::vec3 の x、y、z は public なので、型の外からでも登録できる。保存すると {"x": .., "y": .., "z": ..} になる。
namespace glm
{
    HUSKY_STRUCT_BEGIN(vec3)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
        HUSKY_PROPERTY(z)
    HUSKY_STRUCT_END()
}

namespace husky::engine
{
    struct Transform
    {
        glm::vec3 position = glm::vec3(0.0f); // ワールドでの位置
        float rotation = 0.0f;                // z 軸まわりの角度(度。反時計回りが +)
        glm::vec3 scale = glm::vec3(1.0f);    // 拡大縮小(1 で元の大きさ)
    };

    // 入れ子の構造体(glm::vec3)は、上で先に登録してある。
    HUSKY_STRUCT_BEGIN(Transform)
        HUSKY_PROPERTY(position)
        HUSKY_PROPERTY(rotation)
        HUSKY_PROPERTY(scale)
    HUSKY_STRUCT_END()
}
