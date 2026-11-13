// Collider2D.h の実装。
#include "engine/Collider2D.h"

#include "engine/Actor.h"
#include "engine/GlmConversion.h"
#include "engine/SpriteComponent.h"
#include "engine/TransformMatrix.h"

#include <algorithm> // std::clamp、std::max
#include <cmath>     // std::fabs

namespace husky::engine
{
    ShapeDesc2D Collider2D::MakeShapeDesc() const
    {
        ShapeDesc2D desc;
        desc.offset = offset;
        desc.isTrigger = isTrigger;
        // グループの番号(0〜31)を、ビットの印にする(番号 3 → 1 << 3)
        desc.filter.layer = 1u << static_cast<std::uint32_t>(std::clamp<std::int32_t>(layer, 0, 31));
        // -1 は、ビットで見るとすべてが 1(2 の補数)。整数のまま、ビットの印として読み替える
        desc.filter.mask = static_cast<std::uint32_t>(collisionMask);
        desc.owner = GetHandle();
        FillShape(desc); // 種類と大きさは子クラスが書く(仮想関数)
        return desc;
    }

    void BoxCollider2D::FillShape(ShapeDesc2D& desc) const
    {
        desc.type = ShapeType2D::Box;
        desc.width = size.x;
        desc.height = size.y;
    }

    void CircleCollider2D::FillShape(ShapeDesc2D& desc) const
    {
        desc.type = ShapeType2D::Circle;
        desc.radius = radius;
    }

    render::SpriteCorners ComputeBoxColliderCorners(const Transform& transform, const BoxCollider2D& box)
    {
        render::Sprite shape;
        shape.model = MakeModelMatrix(transform);
        shape.width = box.size.x;
        shape.height = box.size.y;
        // ピボットを offset の分だけずらす: 左の辺 = -幅 x pivotX = -幅 / 2 + offset.x になるように、pivotX = 0.5 - offset.x / 幅
        shape.pivotX = box.size.x != 0.0f ? 0.5f - box.offset.x / box.size.x : 0.5f;
        shape.pivotY = box.size.y != 0.0f ? 0.5f - box.offset.y / box.size.y : 0.5f;
        return render::ComputeSpriteCorners(shape);
    }

    void ComputeCircleColliderWorld(const Transform& transform, const CircleCollider2D& circle, float& centerX, float& centerY,
                                    float& radius)
    {
        // 真ん中: offset の点に、モデル行列(移動 x 回転 x 拡大縮小)を掛ける
        const glm::vec4 center = ToGlm(MakeModelMatrix(transform)) * glm::vec4(circle.offset.x, circle.offset.y, 0.0f, 1.0f);
        centerX = center.x;
        centerY = center.y;
        radius = circle.radius * std::max(std::fabs(transform.scale.x), std::fabs(transform.scale.y));
    }

    void BoxCollider2D::FitTo(const core::Vector2& newSize, const core::Vector2& center)
    {
        size = newSize;
        offset = center;
    }

    void CircleCollider2D::FitTo(const core::Vector2& size, const core::Vector2& center)
    {
        radius = std::max(std::fabs(size.x), std::fabs(size.y)) * 0.5f; // 絵がちょうど収まる(大きいほうの辺に合わせる)
        offset = center;
    }

    bool FitColliderToSprite(Collider2D& collider, const Actor& actor)
    {
        const SpriteComponent* sprite = actor.GetComponent<SpriteComponent>();
        if (sprite == nullptr)
        {
            return false;
        }
        // 絵の四角は、ピボットを原点にして、左が -幅 x pivot.x、右が 幅 x (1 - pivot.x)。真ん中はその平均 = 幅 x (0.5 - pivot.x)
        const core::Vector2 center = {sprite->width * (0.5f - sprite->pivot.x), sprite->height * (0.5f - sprite->pivot.y)};
        collider.FitTo(core::Vector2{sprite->width, sprite->height}, center); // 仮想関数: 四角か円かで合わせ方が変わる
        return true;
    }
}
