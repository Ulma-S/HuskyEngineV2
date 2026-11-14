#include "game/CameraFollowComponent.h"

#include "engine/Actor.h"

namespace husky::game
{
    using engine::Actor; // この .cpp の中だけで、engine::Actor を Actor と書けるようにする

    void CameraFollowComponent::Tick(double dt)
    {
        (void)dt; // すぐに追いつく(遅れて滑らかに追う動きは、要るようになったら足す)
        Actor* owner = GetOwner();
        const Actor* targetActor = FindTarget();
        if (owner == nullptr || targetActor == nullptr)
        {
            return;
        }
        // どちらに親がいても、ワールドの位置で追う(HIER-1)。z はそのまま
        const core::Vector3 targetPosition = targetActor->GetWorldPosition();
        core::Vector3 position = owner->GetWorldPosition();
        position.x = targetPosition.x + offset.x;
        position.y = targetPosition.y + offset.y;
        owner->SetWorldPosition(position);
    }

    const Actor* CameraFollowComponent::FindTarget() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        const Actor* actor = registry != nullptr ? registry->Resolve<Actor>(target) : nullptr;
        if (actor == nullptr || actor->IsPendingDestroy())
        {
            return nullptr;
        }
        return actor;
    }
}
