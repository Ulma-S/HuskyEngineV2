// Actor.h と Component.h の実装。
#include "engine/Actor.h"

#include <algorithm> // std::remove_if

namespace husky::engine
{
    Actor* Component::GetOwner() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Actor>(m_owner) : nullptr;
    }

    void Actor::DestroyComponents()
    {
        for (const core::ObjectHandle handle : components)
        {
            if (Component* component = ResolveComponent<Component>(handle))
            {
                component->Destroy(); // 何度呼んでもよい(すでに予約済みなら何もしない)
            }
        }
    }

    std::size_t Actor::RemoveDestroyedComponents()
    {
        const std::size_t before = components.size();
        components.erase(std::remove_if(components.begin(), components.end(),
                                        [this](core::ObjectHandle handle) {
                                            const Component* component = ResolveComponent<Component>(handle);
                                            return component == nullptr || component->IsPendingDestroy();
                                        }),
                         components.end());
        return before - components.size();
    }
}
