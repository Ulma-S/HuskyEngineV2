// 2D の当たり判定(と、将来の物理)の世界のインターフェース(COL-2)。
// エンジンとゲームは、このヘッダの型だけを使う。中身(今は Box2D)の型と関数は、中身の .cpp の中だけに閉じ込める
// (設計メモ「当たり判定と物理」)。中身を替えるときは、CreatePhysicsWorld2D が返すものを替えるだけ。
//
// 使い方:
//   std::unique_ptr<PhysicsWorld2D> world = CreatePhysicsWorld2D();
//   ShapeDesc2D box;                 // 四角(幅 width、高さ height)。owner に持ち主のアクターのハンドル
//   box.owner = actor->GetHandle();
//   PhysicsShapeHandle2D shape = world->AddShape(box, MakePose2D(actor->transform));
//   world->SetPose(shape, MakePose2D(actor->transform)); // アクターが動いたら、毎フレーム伝える
//   world->Step(dt);
//   world->OverlapPoint(x, y, AllCollisionLayers, hits); // 点に重なる形を探す
//
// 形は、持ち主の transform(位置・回転・拡大縮小)に合わせて置く。大きさ(width、height、radius)と offset は、拡大縮小の前の値。
// 四角は回転・拡大縮小にそのまま合わせ、円は縦横の大きいほうの拡大に合わせる(Unity と同じ)。
//
// 今は「当たり判定だけ」: 形はどれも動かされるだけで(自分では落ちない・跳ねない)、問い合わせで重なりを調べる。
// 物理(Rigidbody2D)は後から足す(Step はそのときに意味を持つ。呼ぶ場所を今から1か所にしておく)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Object.h" // core::ObjectHandle
#include "core/Vector2.h"
#include "engine/PhysicsTypes.h"
#include "engine/Transform.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace husky::engine
{
    // 世界に置いた形を指すハンドル。中身の ID を、中身を知らない形(整数)で持つ。0 は「何も指さない」。
    struct PhysicsShapeHandle2D
    {
        std::uint64_t id = 0;

        bool IsNull() const { return id == 0; }
        bool operator==(const PhysicsShapeHandle2D& other) const { return id == other.id; }
        bool operator!=(const PhysicsShapeHandle2D& other) const { return id != other.id; }
    };

    // 形の種類。複雑な形(カプセル、多角形)は後から足す。
    enum class ShapeType2D
    {
        Box,
        Circle,
    };

    // 置く形の決め事(拡大縮小の前の値)。
    struct ShapeDesc2D
    {
        ShapeType2D type = ShapeType2D::Box;
        float width = 32.0f;   // 四角の幅(Box のとき)
        float height = 32.0f;  // 四角の高さ(Box のとき)
        float radius = 16.0f;  // 円の半径(Circle のとき)
        core::Vector2 offset;  // 持ち主の位置からのずらし(持ち主と一緒に回り、拡大される)
        CollisionFilter filter;
        bool isTrigger = false;   // 重なりを知らせるだけで、物理では押し合わない(Box2D のセンサー。PHY-3)
        // 材質(物理で押し合うときに使う。Box2D の既定と同じ値)
        float density = 1.0f;     // 密度(重さ = 面積 × 密度。面積はワールドの単位で測る: 32 × 32 の四角なら 1024)
        float friction = 0.6f;    // 摩擦(0 でつるつる、1 くらいでざらざら)
        float restitution = 0.0f; // 跳ね返り(0 で跳ねない、1 で同じ速さで跳ね返る)
        core::ObjectHandle owner; // 持ち主のアクター(問い合わせの結果で返す。弱い参照)
    };

    // 形を置く場所: 持ち主の transform のうち、2D で使う値。
    struct Pose2D
    {
        core::Vector2 position;
        float rotationDegrees = 0.0f; // 反時計回りが +
        core::Vector2 scale = {1.0f, 1.0f};
    };

    // アクターの transform から Pose2D を作る(z は使わない)。
    Pose2D MakePose2D(const Transform& transform);

    // 重なりの問い合わせの結果 1 つ。
    struct QueryHit2D
    {
        PhysicsShapeHandle2D shape;
        core::ObjectHandle owner;
    };

    // レイ(線分)の問い合わせの結果。
    struct RaycastHit2D
    {
        PhysicsShapeHandle2D shape;
        core::ObjectHandle owner;
        core::Vector2 point;   // 当たった点
        core::Vector2 normal;  // 当たった面の向き(外向き、長さ 1)
        float fraction = 0.0f; // 線分のどこで当たったか(始点 0 〜 終点 1)
    };

    // 世界に置いた体を指すハンドル(PHY-1)。形のハンドルと同じく、中身の ID を整数で持つ。0 は「何も指さない」。
    struct PhysicsBodyHandle2D
    {
        std::uint64_t id = 0;

        bool IsNull() const { return id == 0; }
        bool operator==(const PhysicsBodyHandle2D& other) const { return id == other.id; }
        bool operator!=(const PhysicsBodyHandle2D& other) const { return id != other.id; }
    };

    // 体の種類(Unity の Rigidbody2D の Body Type と同じ)。
    enum class BodyType2D
    {
        Static,    // 動かない(壁、床)。押されても動かない
        Kinematic, // コードが動かす。物理の体を押すが、押し返されない(重さが無限大のように振る舞う)
        Dynamic,   // 物理で動く(重力、速度、押し合い)
    };

    // 置く体の決め事。
    struct BodyDesc2D
    {
        BodyType2D type = BodyType2D::Dynamic;
        float gravityScale = 1.0f;   // 重力の倍率(0 なら重力を受けない)
        float linearDamping = 0.0f;  // 進む速さの減り方(空気抵抗のようなもの。0 で減らない)
        float angularDamping = 0.0f; // 回る速さの減り方
        bool fixedRotation = false;  // true なら回らない(ぶつかっても傾かない)
    };

    // 物理で押し合う 2 つの形が触れ始めた・離れたこと(PHY-3)。トリガー(isTrigger)の形と、
    // Dynamic を含まない組(kinematic どうし、static と kinematic)では起きない。
    struct ContactEvent2D
    {
        bool begin = true;
        core::ObjectHandle ownerA; // 形 A の持ち主(ShapeDesc2D::owner)。離れたとき、形がもう消えていれば null
        core::ObjectHandle ownerB;
        core::Vector2 normal;      // A から B へ向かう向き(長さ 1)。離れたときは 0
        core::Vector2 point;       // 触れた点。離れたときは 0
    };

    class PhysicsWorld2D
    {
    public:
        virtual ~PhysicsWorld2D() = default;

        // 形を置く。戻り値で、後から動かす・消す・結果と照らし合わせる。
        virtual PhysicsShapeHandle2D AddShape(const ShapeDesc2D& desc, const Pose2D& pose) = 0;
        // 形の場所を変える(持ち主が動いた・回った・拡大縮小した)。知らないハンドルなら何もしない。
        virtual void SetPose(PhysicsShapeHandle2D shape, const Pose2D& pose) = 0;
        // 形を消す。知らないハンドルなら何もしない。
        virtual void RemoveShape(PhysicsShapeHandle2D shape) = 0;
        // 置いている形の数。
        virtual std::size_t GetShapeCount() const = 0;

        // ---- 体(PHY-1。物理で動くもの)----
        // 上の AddShape は、形ごとに「コードが動かす体」を中で1つ作る(当たり判定だけのもの)。
        // 物理で動かすときは、体を置いてから形を付ける(1つの体に形をいくつも付けられる)。

        // 体を置く。pose の拡大縮小は、この体に付ける形の大きさに効く。
        virtual PhysicsBodyHandle2D AddBody(const BodyDesc2D& desc, const Pose2D& pose) = 0;
        // 体に形を付ける(形の場所は、体からのずらし desc.offset)。知らない体なら null を返す。
        // 付けた形は SetPose では動かない(体と一緒に動く)。RemoveShape で形だけを外せる。
        virtual PhysicsShapeHandle2D AttachShape(PhysicsBodyHandle2D body, const ShapeDesc2D& desc) = 0;
        // 体を消す(付いている形も消える)。知らない体なら何もしない。
        virtual void RemoveBody(PhysicsBodyHandle2D body) = 0;
        // AddBody で置いた体の数(AddShape が中で作る体は数えない)。
        virtual std::size_t GetBodyCount() const = 0;

        // 体を瞬間移動させる(速度はそのまま)。拡大縮小が変わったら、付いている形を作り直す。
        virtual void SetBodyPose(PhysicsBodyHandle2D body, const Pose2D& pose) = 0;
        // 体の今の場所と回転(拡大縮小は最後に渡した値)。知らない体なら既定の値。
        virtual Pose2D GetBodyPose(PhysicsBodyHandle2D body) const = 0;
        // Kinematic の体を、次の 1 歩(stepSeconds 秒)でちょうど pose に着く速さにする。
        // 瞬間移動と違い、通り道にある Dynamic の体を押しのける(コードで動かすものが物理の体を押すときに使う)。
        virtual void MoveBodyTo(PhysicsBodyHandle2D body, const Pose2D& pose, float stepSeconds) = 0;

        // 速度(単位/秒)と回る速さ(度/秒、反時計回りが +)。
        virtual void SetLinearVelocity(PhysicsBodyHandle2D body, const core::Vector2& velocity) = 0;
        virtual core::Vector2 GetLinearVelocity(PhysicsBodyHandle2D body) const = 0;
        virtual void SetAngularVelocity(PhysicsBodyHandle2D body, float degreesPerSecond) = 0;
        virtual float GetAngularVelocity(PhysicsBodyHandle2D body) const = 0;
        // 重心を押す。力(force)は次の 1 歩の間ずっと押し続け、衝撃(impulse)は一瞬で速さを変える(速さの変化 = 衝撃 / 重さ)。
        virtual void ApplyForce(PhysicsBodyHandle2D body, const core::Vector2& force) = 0;
        virtual void ApplyImpulse(PhysicsBodyHandle2D body, const core::Vector2& impulse) = 0;
        // 重さ(付いている形の 面積 × 密度 の合計)。Static と Kinematic は 0。
        virtual float GetMass(PhysicsBodyHandle2D body) const = 0;

        // 重力(単位/秒²。下向きなら y がマイナス)。既定は 0(重力を使うかはシーン・ゲームが決める)。
        virtual void SetGravity(const core::Vector2& gravity) = 0;
        virtual core::Vector2 GetGravity() const = 0;

        // 時間を dt 秒進める(Dynamic の体が動く)。決まった刻み(FixedStepper)で呼ぶ。
        // 進めている間に形どうしが触れ始めた・離れたことは、世界の中に貯まる(TakeContactEvents で受け取る)。
        virtual void Step(float dt) = 0;
        // 前に受け取ってから貯まった接触のイベントを out に移す(前の中身は消す。世界の中は空になる)(PHY-3)。
        virtual void TakeContactEvents(std::vector<ContactEvent2D>& out) = 0;

        // 点 (x, y) に重なる形を out に入れる(前の中身は消す)。mask に当てはまるレイヤーの形だけ。
        virtual void OverlapPoint(float x, float y, std::uint32_t mask, std::vector<QueryHit2D>& out) const = 0;
        // desc の形を pose に置いたとしたら、重なる形を out に入れる(前の中身は消す)。
        virtual void OverlapShape(const ShapeDesc2D& desc, const Pose2D& pose, std::uint32_t mask,
                                  std::vector<QueryHit2D>& out) const = 0;
        // (fromX, fromY) から (toX, toY) への線分で、いちばん手前に当たる形を探す。当たれば true と out。
        virtual bool Raycast(float fromX, float fromY, float toX, float toY, std::uint32_t mask, RaycastHit2D& out) const = 0;
    };

    // 世界を作る(今の中身は Box2D)。
    std::unique_ptr<PhysicsWorld2D> CreatePhysicsWorld2D();
}
