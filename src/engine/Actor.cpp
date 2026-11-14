// Actor.h と Component.h の実装。
#include "engine/Actor.h"

#include "engine/Scene.h" // GetScene で Scene* を返すために、Scene の中身(Object の子であること)を知る必要がある

#include "engine/EngineLog.h"
#include "engine/GlmConversion.h"
#include "engine/Rigidbody2D.h"
#include "engine/TransformMatrix.h"

#include <glm/matrix.hpp> // glm::inverse

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

    // ---- 親子の階層(HIER-1)----

    Actor* Actor::GetParent() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Actor>(m_parent) : nullptr;
    }

    bool Actor::SetParent(Actor* parent, bool keepWorldTransform)
    {
        const char* refused = nullptr;
        if (parent != nullptr)
        {
            if (parent->GetRegistry() != GetRegistry() || GetRegistry() == nullptr)
            {
                refused = "別のレジストリのアクターです";
            }
            else if (parent->IsPendingDestroy() || IsPendingDestroy())
            {
                refused = "消えかけのアクターです";
            }
            else if (GetComponent<Rigidbody2D>() != nullptr)
            {
                refused = "Rigidbody2D を持つアクター(動く体)は親を持てません";
            }
            else
            {
                // 親の親…とたどり、自分に行き着いたら輪になる(自分を親にするのも、ここで見つかる)
                for (const Actor* ancestor = parent; ancestor != nullptr; ancestor = ancestor->GetParent())
                {
                    if (ancestor == this)
                    {
                        refused = "自分か自分の子孫は親にできません(輪になります)";
                        break;
                    }
                }
            }
        }
        if (refused != nullptr)
        {
            HUSKY_LOG(SceneLog::ParentRefused, name, parent->name, refused);
            return false;
        }

        // 付け替える前のワールドの値を控える(付け替えた後に、同じ見え方になるよう transform を直すため)
        const Transform world = GetWorldTransform();
        if (Actor* old = GetParent())
        {
            old->RemoveChild(GetHandle());
        }
        m_parent = parent != nullptr ? parent->GetHandle() : core::ObjectHandle{};
        if (parent != nullptr)
        {
            parent->m_children.push_back(GetHandle());
        }
        if (keepWorldTransform)
        {
            SetWorldTransform(world);
        }
        ClearRenderTransform(); // 補間の基準がローカルの値なので、付け替えたら混ぜない
        return true;
    }

    core::Matrix4 Actor::GetWorldMatrix() const
    {
        const core::Matrix4 local = MakeModelMatrix(transform);
        const Actor* parent = GetParent();
        if (parent == nullptr)
        {
            return local;
        }
        // 右から順に効く: まず自分のローカルの行列で親から見た場所へ、次に親のワールドの行列でワールドへ
        return FromGlm(ToGlm(parent->GetWorldMatrix()) * ToGlm(local));
    }

    core::Matrix4 Actor::GetRenderWorldMatrix() const
    {
        const core::Matrix4 local = MakeModelMatrix(GetRenderTransform());
        const Actor* parent = GetParent();
        if (parent == nullptr)
        {
            return local;
        }
        return FromGlm(ToGlm(parent->GetRenderWorldMatrix()) * ToGlm(local));
    }

    Transform Actor::GetWorldTransform() const
    {
        if (GetParent() == nullptr)
        {
            return transform; // 親が無い: 分け直さずにそのまま返す(角度 370 度なども、そのまま保つ)
        }
        return DecomposeModelMatrix(GetWorldMatrix());
    }

    void Actor::SetWorldTransform(const Transform& world)
    {
        const Actor* parent = GetParent();
        if (parent == nullptr)
        {
            transform = world;
            return;
        }
        // ワールド = 親 × ローカル なので、ローカル = 親の逆行列 × ワールド
        const glm::mat4 local = glm::inverse(ToGlm(parent->GetWorldMatrix())) * ToGlm(MakeModelMatrix(world));
        transform = DecomposeModelMatrix(FromGlm(local));
    }

    core::Vector3 Actor::GetWorldPosition() const
    {
        const Actor* parent = GetParent();
        if (parent == nullptr)
        {
            return transform.position;
        }
        const glm::vec4 world = ToGlm(parent->GetWorldMatrix()) * glm::vec4(ToGlm(transform.position), 1.0f);
        return {world.x, world.y, world.z};
    }

    void Actor::SetWorldPosition(const core::Vector3& position)
    {
        const Actor* parent = GetParent();
        if (parent == nullptr)
        {
            transform.position = position;
            return;
        }
        // 点を親のワールドの行列の逆で、親から見た位置へ戻す
        const glm::vec4 local = glm::inverse(ToGlm(parent->GetWorldMatrix())) * glm::vec4(ToGlm(position), 1.0f);
        transform.position = {local.x, local.y, local.z};
    }

    void Actor::RemoveChild(core::ObjectHandle child)
    {
        m_children.erase(std::remove(m_children.begin(), m_children.end(), child), m_children.end());
    }

    void Actor::DetachFromHierarchy()
    {
        // 子を先に親なしにする(自分がまだ解決できるうちに、子のワールドの値を計算する)
        core::ObjectRegistry* registry = GetRegistry();
        const std::vector<core::ObjectHandle> children = m_children; // SetParent が m_children を書き換えるので写す
        for (const core::ObjectHandle handle : children)
        {
            if (Actor* child = registry != nullptr ? registry->Resolve<Actor>(handle) : nullptr)
            {
                child->SetParent(nullptr, true);
            }
        }
        m_children.clear();
        if (Actor* parent = GetParent())
        {
            parent->RemoveChild(GetHandle());
        }
        m_parent = core::ObjectHandle{};
    }
}
