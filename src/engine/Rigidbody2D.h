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
    HUSKY_ENUM_DECLARE(BodyType2D) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)

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

        // ---- 速度と力(PHY-2b。保存しない。物理の世界の体を読み書きする)----
        // 体がまだ無い(置いた同じフレーム、編集中)ときの速度は控えておき、体ができたときに入れる。
        // 力・押す力は、体が無ければ何もしない。
        void SetLinearVelocity(const core::Vector2& velocity); // 単位/秒
        core::Vector2 GetLinearVelocity() const;
        void SetAngularVelocity(float degreesPerSecond); // 反時計回りが +
        float GetAngularVelocity() const;
        void ApplyForce(const core::Vector2& force);     // 次の 1 歩に掛かる力(押し続けるなら毎フレーム)
        void ApplyImpulse(const core::Vector2& impulse); // 一瞬で速さを変える力(重さで割った分だけ速さが変わる)
        float GetMass() const;                           // 面積 × 密度(体が無ければ 0)
        // 物理の世界に体があるか。
        bool HasBody() const { return m_physicsWorld != nullptr && !m_physicsBody.IsNull(); }

        // PhysicsScene2D が、体を置いたときに呼ぶ(世界は借りるだけ)。控えていた速度を体に入れる。
        void ConnectBody(PhysicsWorld2D* world, PhysicsBodyHandle2D body);
        // 親を持つアクターに付いていて使わないことを、もう警告したか(PhysicsScene2D が 1 回だけ警告する。HIER-1。保存しない)
        bool warnedIgnoredAsChild = false;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(bodyType)
            HUSKY_PROPERTY(gravityScale)
            HUSKY_PROPERTY(linearDamping)
            HUSKY_PROPERTY(angularDamping)
            HUSKY_PROPERTY(fixedRotation)
        HUSKY_REFLECTION_END()

    private:
        // 物理の世界(PhysicsScene2D が持つ。借りるだけ)と、その中の体。体ができるまでは nullptr と null。
        PhysicsWorld2D* m_physicsWorld = nullptr;
        PhysicsBodyHandle2D m_physicsBody;
        // 体ができる前に頼まれた速度(体ができたら入れる)
        core::Vector2 m_pendingVelocity;
        float m_pendingAngularVelocity = 0.0f;
    };
}
