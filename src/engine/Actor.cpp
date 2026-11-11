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

    void Actor::TickComponents(double dt)
    {
        // Scene::Tick と同じく、回す数を最初に決め、番号で回す。
        // Tick の中で AddComponent されると components が大きくなり、中身が引っ越すことがあるため。
        const std::size_t count = components.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            Component* component = ResolveComponent<Component>(components[i]);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                component->Tick(dt);
            }
        }
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
