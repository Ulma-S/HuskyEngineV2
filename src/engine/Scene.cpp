// Scene.h の実装。
#include "engine/Scene.h"

#include "core/ObjectRegistry.h"

namespace husky::engine
{
    Scene* Scene::GetParent() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Scene>(m_parent) : nullptr;
    }

    bool Scene::IsSelfOrDescendantOf(const Scene& other) const
    {
        // 親をたどる。シーンの木は World だけがつなぐので輪にならないが、念のため段数に上限を付ける。
        const Scene* current = this;
        for (int depth = 0; current != nullptr && depth < 64; ++depth)
        {
            if (current == &other)
            {
                return true;
            }
            current = current->GetParent();
        }
        return false;
    }
}
