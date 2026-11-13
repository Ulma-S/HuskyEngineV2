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
}
