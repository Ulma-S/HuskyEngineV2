// 2D のコライダー(当たりの形)のコンポーネント(COL-3。Unity の BoxCollider2D / CircleCollider2D にあたる)。
// アクターに付けると、シーンを更新している間、当たり判定の世界(PhysicsScene2D)に形が置かれ、アクターの transform に合わせて動く。
// ほかのアクターのコライダーと重なり始めた・離れたときは、Actor::OnOverlap と Component::OnOverlap が呼ばれる。
//
// ふつうのリフレクション対応のコンポーネントなので、保存、プレハブ、Undo、インスペクタがそのまま使える。
// 大きさと offset は、拡大縮小の前の値(アクターの transform の回転と拡大縮小に合わせて、形も回り・伸びる)。
//
// 使い方:
//   BoxCollider2D* box = actor->AddComponent<BoxCollider2D>();
//   box->size = {32.0f, 32.0f};
//   box->layer = 1;            // グループ 1 に属する
//   box->collisionMask = 1;    // グループ 0 とだけ当たる(ビットの印。-1 ならすべて)
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Vector2.h"
#include "engine/Component.h"
#include "engine/PhysicsWorld2D.h"
#include "engine/Transform.h"
#include "render/RenderDevice.h" // render::SpriteCorners

#include <cstdint>

namespace husky::engine
{
    class Actor;

    // 四角と円に共通の部分。これだけでは形が決まらないので、型の登録簿には入れない(インスペクタで足せるのは子だけ)。
    class Collider2D : public Component
    {
        HUSKY_OBJECT(Collider2D, Component)
    public:
        core::Vector2 offset;              // アクターの位置からのずらし(アクターと一緒に回り、拡大される)
        bool isTrigger = false;            // 重なりを知らせるだけで、物理では押し合わない(今は覚えておくだけ。物理を足すときに効く)
        std::int32_t layer = 0;            // 属するグループの番号(0〜31)
        std::int32_t collisionMask = -1;   // 当たる相手のグループの印(ビット。-1 = すべてのビットが 1 = すべてと当たる)

        // 形を、真ん中が center(アクターの位置からのずらし)、大きさ size の四角に合わせる(子クラスが決める。四角はそのまま、
        // 円は大きいほうの辺に合わせた半径)。絵に合わせるとき(FitColliderToSprite)に使う。
        virtual void FitTo(const core::Vector2& size, const core::Vector2& center) { (void)size; offset = center; }

        // 当たり判定の世界に置く形の決め事を作る(大きさなどは子クラスが決める)。持ち主は、このコンポーネント自身のハンドル。
        ShapeDesc2D MakeShapeDesc() const;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(offset)
            HUSKY_PROPERTY(isTrigger)
            HUSKY_PROPERTY(layer)
            HUSKY_PROPERTY(collisionMask)
        HUSKY_REFLECTION_END()

    protected:
        // 子クラスが、形の種類と大きさを書く。
        virtual void FillShape(ShapeDesc2D& desc) const { (void)desc; }
    };

    // 四角のコライダー。
    class BoxCollider2D : public Collider2D
    {
        HUSKY_OBJECT(BoxCollider2D, Collider2D)
    public:
        core::Vector2 size = {32.0f, 32.0f}; // 幅と高さ(拡大縮小の前)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(size)
        HUSKY_REFLECTION_END()

    protected:
        void FillShape(ShapeDesc2D& desc) const override;

    public:
        void FitTo(const core::Vector2& size, const core::Vector2& center) override;
    };

    // 円のコライダー。
    class CircleCollider2D : public Collider2D
    {
        HUSKY_OBJECT(CircleCollider2D, Collider2D)
    public:
        float radius = 16.0f; // 半径(拡大縮小の前。縦横で違う拡大では、大きいほうに合わせる)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(radius)
        HUSKY_REFLECTION_END()

    protected:
        void FillShape(ShapeDesc2D& desc) const override;

    public:
        void FitTo(const core::Vector2& size, const core::Vector2& center) override;
    };

    // 四角のコライダーの四隅のワールドの位置(エディタの枠を描くため)。描画と同じ道具(MakeModelMatrix と ComputeSpriteCorners)で求める。
    // offset は、ピボットをずらすことで表す(ピボットを原点にした四角形を、offset の分だけ反対へずらすのと同じ)。
    render::SpriteCorners ComputeBoxColliderCorners(const Transform& transform, const BoxCollider2D& box);

    // 円のコライダーのワールドでの真ん中と半径(エディタの枠を描くため)。半径は、縦横の大きいほうの拡大に合わせる。
    void ComputeCircleColliderWorld(const Transform& transform, const CircleCollider2D& circle, float& centerX, float& centerY,
                                    float& radius);

    // コライダーを、同じアクターのスプライト(SpriteComponent)の絵の大きさと位置に合わせる(Unity でコライダーを足したときと同じ)。
    // 絵の真ん中は、ピボットからのずれ((0.5 - pivot) x 大きさ)なので、それを offset にする。スプライトが無ければ何もせず false。
    // エディタでコライダーを足したとき(component.add、インスペクタ)に呼ぶ。コードの AddComponent では呼ばない(作る側が決める)。
    bool FitColliderToSprite(Collider2D& collider, const Actor& actor);
}
