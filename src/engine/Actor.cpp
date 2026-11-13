// Actor.h と Component.h の実装。
#include "engine/Actor.h"

#include "engine/Scene.h" // GetScene で Scene* を返すために、Scene の中身(Object の子であること)を知る必要がある

#include <algorithm> // std::remove_if, std::find

namespace husky::engine
{
    Actor* Component::GetOwner() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Actor>(m_owner) : nullptr;
    }

    Scene* Actor::GetScene() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Scene>(m_scene) : nullptr;
    }

    void Actor::RelinkComponentOwners()
    {
        for (const core::ObjectHandle handle : components)
        {
            if (Component* component = ResolveComponent<Component>(handle))
            {
                // Component の m_owner は private だが、Actor は Component の friend なので書ける。
                // Component& として書くのは、AddComponent と同じく、名前の隠蔽を避けるため。
                Component& base = *component;
                base.m_owner = GetHandle();
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

    std::size_t Actor::InsertComponent(Component& component, std::size_t index)
    {
        // Component& のまま書く(子クラスに同じ名前のメンバーがあっても、Component の m_owner を指すように)。
        component.m_owner = GetHandle();
        const std::size_t at = index < components.size() ? index : components.size();
        // insert: at 番目の前に入れる(at 番目から後ろは1つずつ後ろへずれる)。C# の List<T>.Insert と同じ。
        components.insert(components.begin() + static_cast<std::ptrdiff_t>(at), component.GetHandle());
        return at;
    }

    std::int64_t Actor::RemoveComponent(Component& component)
    {
        const auto found = std::find(components.begin(), components.end(), component.GetHandle());
        if (found == components.end())
        {
            return -1;
        }
        const std::int64_t index = found - components.begin();
        components.erase(found);
        component.Destroy();
        return index;
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
