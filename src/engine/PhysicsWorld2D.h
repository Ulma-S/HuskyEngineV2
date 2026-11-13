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
        bool isTrigger = false;   // 重なりを知らせるだけで、物理では押し合わない(今は覚えておくだけ。物理を足すときに効く)
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

        // 時間を dt 秒進める(今は物理が無いので、中身の準備だけ。物理を足したら、決まった刻みで呼ぶ)。
        virtual void Step(float dt) = 0;

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
