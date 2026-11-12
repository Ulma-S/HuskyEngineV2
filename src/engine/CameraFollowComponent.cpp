#include "engine/CameraFollowComponent.h"

#include "engine/Actor.h"

namespace husky::engine
{
    void CameraFollowComponent::Tick(double dt)
    {
        (void)dt; // すぐに追いつく(遅れて滑らかに追う動きは、要るようになったら足す)
        Actor* owner = GetOwner();
        const Actor* targetActor = FindTarget();
        if (owner == nullptr || targetActor == nullptr)
        {
            return;
        }
        owner->transform.position.x = targetActor->transform.position.x + offset.x;
        owner->transform.position.y = targetActor->transform.position.y + offset.y;
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
