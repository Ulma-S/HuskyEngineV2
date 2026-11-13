// 2D の物理の体のコンポーネント(PHY-2。Unity の Rigidbody2D にあたる)。
// アクターに付けると、シーンを更新している間、そのアクターは物理の世界(PhysicsScene2D)の「体」になり、
// 同じアクターのコライダー(BoxCollider2D、CircleCollider2D)はすべてその体に付く。
//
// 体の種類(bodyType):
//   Dynamic   物理で動く。1 歩ごとに、物理が出した位置と回転をアクターの transform に書き戻す(Box2D → アクター)。
//             コードがアクターの位置を書き換えたら、瞬間移動として物理に伝える(アクター → Box2D)。
//   Kinematic コードが動かす(アクター → Box2D)。
//   Static    動かない(壁、床)。アクターを動かしたときだけ、瞬間移動として伝える。
// Rigidbody2D の無いコライダーは、今までどおり「コードが動かす形」(1 つずつ kinematic の体)として置かれる。
//
// 使い方:
//   Rigidbody2D* body = actor->AddComponent<Rigidbody2D>();
//   body->bodyType = BodyType2D::Dynamic;
//   actor->AddComponent<BoxCollider2D>();  // 当たりの形(重さは 面積 × 密度)
//
// ふつうのリフレクション対応のコンポーネントなので、保存、プレハブ、Undo、インスペクタがそのまま使える。
// 1 つのアクターに 2 つ付けたときは、最初のものだけを使う。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/EnumReflection.h"
#include "engine/Component.h"
#include "engine/PhysicsWorld2D.h"

namespace husky::engine
{
    // 体の種類の名前の表(保存は "Dynamic" のような名前。インスペクタは名前から選ぶ)。
    // BodyType2D と同じ名前空間(husky::engine)に書く(ADL で見つけるため)。
    HUSKY_ENUM_BEGIN(BodyType2D)
        HUSKY_ENUM_VALUE(Static)
        HUSKY_ENUM_VALUE(Kinematic)
        HUSKY_ENUM_VALUE(Dynamic)
    HUSKY_ENUM_END()

    class Rigidbody2D : public Component
    {
        HUSKY_OBJECT(Rigidbody2D, Component)
    public:
        BodyType2D bodyType = BodyType2D::Dynamic;
        float gravityScale = 1.0f;   // 重力の倍率(0 なら重力を受けない。重力そのものはシーンの設定で、既定は 0)
        float linearDamping = 0.0f;  // 進む速さの減り方(0 で減らない)
        float angularDamping = 0.0f; // 回る速さの減り方
        bool fixedRotation = false;  // true なら回らない(ぶつかっても傾かない)

        // 物理の世界に置く体の決め事を作る。
        BodyDesc2D MakeBodyDesc() const
        {
            BodyDesc2D desc;
            desc.type = bodyType;
            desc.gravityScale = gravityScale;
            desc.linearDamping = linearDamping;
            desc.angularDamping = angularDamping;
            desc.fixedRotation = fixedRotation;
            return desc;
        }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(bodyType)
            HUSKY_PROPERTY(gravityScale)
            HUSKY_PROPERTY(linearDamping)
            HUSKY_PROPERTY(angularDamping)
            HUSKY_PROPERTY(fixedRotation)
        HUSKY_REFLECTION_END()
    };
}
