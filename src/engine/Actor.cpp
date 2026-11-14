// Actor.h と Component.h の実装。
#include "engine/Actor.h"

#include "engine/World.h" // GetWorld で World* を返すために、World の中身(Object の子であること)を知る必要がある

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

    World* Actor::GetWorld() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<World>(m_world) : nullptr;
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


    // ---- 親子の階層(HIER-1、HIER-2a)----

    Actor* Actor::GetParent() const
    {
        core::ObjectRegistry* registry = GetRegistry();
        return registry != nullptr ? registry->Resolve<Actor>(parent) : nullptr;
    }

    bool Actor::IsDescendantOf(const Actor& ancestor) const
    {
        // 親、親の親…とたどる(輪になっていても、上限の段数で止まる)
        const Actor* current = GetParent();
        for (int depth = 0; current != nullptr && depth < MaxHierarchyDepth; ++depth)
        {
            if (current == &ancestor)
            {
                return true;
            }
            current = current->GetParent();
        }
        return false;
    }

    void Actor::GetChildren(std::vector<Actor*>& out) const
    {
        out.clear();
        World* scene = GetWorld();
        if (scene == nullptr)
        {
            return;
        }
        scene->ForEachActor([&](Actor& actor) {
            if (actor.GetParent() == this)
            {
                out.push_back(&actor);
            }
        });
    }

    void Actor::GetDescendants(std::vector<Actor*>& out) const
    {
        World* scene = GetWorld();
        if (scene == nullptr)
        {
            return;
        }
        // 親が先になるよう、見つけた順に1段ずつ広げる(幅優先)。first から後ろが、まだ子を調べていないもの
        std::size_t first = out.size();
        scene->ForEachActor([&](Actor& actor) {
            if (actor.GetParent() == this)
            {
                out.push_back(&actor);
            }
        });
        // 段数の上限で止める(輪になっていても、終わるように)
        for (int depth = 1; depth < MaxHierarchyDepth && first < out.size(); ++depth)
        {
            const std::size_t end = out.size();
            scene->ForEachActor([&](Actor& actor) {
                Actor* actorParent = actor.GetParent();
                if (actorParent == nullptr || &actor == this)
                {
                    return;
                }
                for (std::size_t i = first; i < end; ++i)
                {
                    if (out[i] == actorParent)
                    {
                        out.push_back(&actor);
                        return;
                    }
                }
            });
            first = end;
        }
    }

    int Actor::GetHierarchyDepth() const
    {
        int depth = 0;
        for (const Actor* current = GetParent(); current != nullptr && depth < MaxHierarchyDepth; current = current->GetParent())
        {
            ++depth;
        }
        return depth;
    }

    const char* Actor::WhyCannotSetParent(const Actor* newParent) const
    {
        const char* refused = nullptr;
        if (newParent != nullptr)
        {
            if (newParent->GetRegistry() != GetRegistry() || GetRegistry() == nullptr)
            {
                refused = "別のレジストリのアクターです";
            }
            else if (newParent->IsPendingDestroy() || IsPendingDestroy())
            {
                refused = "消えかけのアクターです";
            }
            else if (GetComponent<Rigidbody2D>() != nullptr)
            {
                refused = "Rigidbody2D を持つアクター(動く体)は親を持てません";
            }
            else if (newParent == this || newParent->IsDescendantOf(*this))
            {
                refused = "自分か自分の子孫は親にできません(輪になります)";
            }
            else if (newParent->GetHierarchyDepth() + 1 >= MaxHierarchyDepth)
            {
                refused = "親子の段数が多すぎます";
            }
        }
        return refused;
    }

    bool Actor::SetParent(Actor* newParent, bool keepWorldTransform, const char** whyRefused)
    {
        const char* refused = WhyCannotSetParent(newParent);
        if (whyRefused != nullptr)
        {
            *whyRefused = refused;
        }
        if (refused != nullptr)
        {
            HUSKY_LOG(SceneLog::ParentRefused, name, newParent->name, refused);
            return false;
        }

        // 付け替える前のワールドの値を控える(付け替えた後に、同じ見え方になるよう transform を直すため)
        const Transform world = GetWorldTransform();
        parent = newParent != nullptr ? newParent->GetHandle() : core::ObjectHandle{};
        if (keepWorldTransform)
        {
            SetWorldTransform(world);
        }
        ClearRenderTransform(); // 補間の基準がローカルの値なので、付け替えたら混ぜない
        return true;
    }

    core::Matrix4 Actor::ComputeWorldMatrix(bool render, int depth) const
    {
        const core::Matrix4 local = MakeModelMatrix(render ? GetRenderTransform() : transform);
        const Actor* actorParent = GetParent();
        if (actorParent == nullptr || depth >= MaxHierarchyDepth)
        {
            return local; // 根(か、輪になっていて上限まで来た)
        }
        // 右から順に効く: まず自分のローカルの行列で親から見た場所へ、次に親のワールドの行列でワールドへ
        return FromGlm(ToGlm(actorParent->ComputeWorldMatrix(render, depth + 1)) * ToGlm(local));
    }

    core::Matrix4 Actor::GetWorldMatrix() const
    {
        return ComputeWorldMatrix(false, 0);
    }

    core::Matrix4 Actor::GetRenderWorldMatrix() const
    {
        return ComputeWorldMatrix(true, 0);
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
        const Actor* actorParent = GetParent();
        if (actorParent == nullptr)
        {
            transform = world;
            return;
        }
        // ワールド = 親 × ローカル なので、ローカル = 親の逆行列 × ワールド
        const glm::mat4 local = glm::inverse(ToGlm(actorParent->GetWorldMatrix())) * ToGlm(MakeModelMatrix(world));
        transform = DecomposeModelMatrix(FromGlm(local));
    }

    core::Vector3 Actor::GetWorldPosition() const
    {
        const Actor* actorParent = GetParent();
        if (actorParent == nullptr)
        {
            return transform.position;
        }
        const glm::vec4 world = ToGlm(actorParent->GetWorldMatrix()) * glm::vec4(ToGlm(transform.position), 1.0f);
        return {world.x, world.y, world.z};
    }

    void Actor::SetWorldPosition(const core::Vector3& position)
    {
        const Actor* actorParent = GetParent();
        if (actorParent == nullptr)
        {
            transform.position = position;
            return;
        }
        // 点を親のワールドの行列の逆で、親から見た位置へ戻す
        const glm::vec4 local = glm::inverse(ToGlm(actorParent->GetWorldMatrix())) * glm::vec4(ToGlm(position), 1.0f);
        transform.position = {local.x, local.y, local.z};
    }
}
