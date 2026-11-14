// アクターの Transform(位置・回転・拡大縮小)から、モデル行列(ローカル → ワールド)を作る(RS-1)。
// データの型(Transform、Matrix4)は glm に依存させず、計算する .cpp の中でだけ glm に変える(engine/GlmConversion.h)。
//
// モデル行列 = 移動 × 回転 × 拡大縮小。点に掛けると、右から順に効く:
//   1. 拡大縮小(ピボットを中心に大きくする)
//   2. 回転(ピボットを中心に、z 軸まわりに回す。反時計回りが +)
//   3. 移動(ピボットをアクターの位置へ運ぶ)
// 順番を逆にすると、「移動してから回す」= 原点を中心に大きく振り回すことになり、その場で回らない。
//
// 動作スレッド: どこでもよい(ただの計算)
#pragma once

#include "engine/Transform.h"

#include "core/Matrix4.h"

namespace husky::engine
{
    core::Matrix4 MakeModelMatrix(const Transform& transform);

    // モデル行列を、位置・回転・拡大縮小に分け直す(MakeModelMatrix の逆。HIER-1)。
    // 親子の階層で、ワールドの行列からワールドの値を取り出すとき、ワールドの値をローカルに直すときに使う。
    // 回転は -180〜180 度。左右の反転(拡大縮小のマイナス)は scale.y のマイナスで表す(x を反転した行列は、
    // 「180 度回して y を反転」と同じなので、どちらで表しても見た目は同じ)。
    // 親の拡大縮小が縦横で違い子が回っていると、行列はゆがむ(せん断)。そのときは x の軸の向きと長さを正しくし、
    // y は x の軸に直角な成分だけを使う(面積は保つ)。
    Transform DecomposeModelMatrix(const core::Matrix4& model);
}
