// World.h の実装。
#include "engine/World.h"

#include "engine/EngineLog.h"
#include "engine/Scene.h"

#include <algorithm> // std::remove, std::remove_if, std::sort

namespace husky::engine
{
    void World::Tick(double dt)
    {
        core::ObjectRegistry* registry = GetRegistry();
        if (registry == nullptr)
        {
            return;
        }

        // 1. 順番表を作る。フレームの最初の時点のアクターとコンポーネントだけが載る。
        //    そのため、Tick の途中で Spawn / AddComponent されたものは、次のフレームまで呼ばれない。
        //    clear は中身を空にするだけで、確保したメモリは手放さない(次のフレームで使い回せる)。
        m_tickEntries.clear();
        std::uint32_t sequence = 0;
        for (const core::ObjectHandle actorHandle : actors)
        {
            const Actor* actor = ResolveActor(actorHandle);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            // アクター自身の Tick。同じ Default のコンポーネントより先になるように、先に載せる。
            m_tickEntries.push_back({TickOrder::Default, sequence++, actorHandle, core::ObjectHandle{}, true});
            for (const core::ObjectHandle componentHandle : actor->components)
            {
                const Component* component = registry->Resolve<Component>(componentHandle);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    m_tickEntries.push_back({component->GetTickOrder(), sequence++, actorHandle, componentHandle, false});
                }
            }
        }

        // 2. 実行順で並べる。実行順が同じなら sequence(一覧の順)で並べるので、毎回同じ順番になる。
        //    std::sort に渡しているのはラムダ式で、「a を b より先にするなら true」を返す比べ方。
        std::sort(m_tickEntries.begin(), m_tickEntries.end(), [](const TickEntry& a, const TickEntry& b) {
            if (a.order != b.order)
            {
                return a.order < b.order;
            }
            return a.sequence < b.sequence;
        });

        // 3. 順番に呼ぶ。呼ぶ直前に毎回、Destroy されていないかを確かめる。
        //    前の行の Tick の中で、アクターやコンポーネントが Destroy されることがあるため。
        //    (順番表は Tick の中で書き換わらないので、範囲 for で回してよい。Spawn で変わるのは actors のほう)
        for (const TickEntry& entry : m_tickEntries)
        {
            Actor* actor = ResolveActor(entry.actor);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue; // アクターが Destroy されたら、そのコンポーネントも呼ばない
            }
            if (entry.isActor)
            {
                actor->Tick(dt);
                continue;
            }
            Component* component = registry->Resolve<Component>(entry.component);
            if (component != nullptr && !component->IsPendingDestroy())
            {
                component->Tick(dt);
            }
        }
    }

    bool World::AddLoadedActor(Actor& actor)
    {
        core::ObjectRegistry* registry = GetRegistry();
        // 同じレジストリのものでなければ、ハンドルが別の表を指してしまうので入れない。
        if (registry == nullptr || registry->Resolve<Actor>(actor.GetHandle()) != &actor)
        {
            return false;
        }
        if (!actor.m_world.IsNull()) // もうどこかのシーンにいる
        {
            return false;
        }
        actor.m_world = GetHandle();
        actor.m_scene = GetRootSceneHandle(); // 置き場所は呼ぶ側が MoveToScene で変える(シーンの読み込み)
        if (actor.guid.IsNull()) // 古いファイルや、コマンドで作ったアクターには、まだ無い
        {
            actor.guid = core::Guid::Generate();
        }
        actor.RelinkComponentOwners();
        actors.push_back(actor.GetHandle());
        return true;
    }

    Actor* World::FindActorByGuid(const core::Guid& guid)
    {
        if (guid.IsNull())
        {
            return nullptr;
        }
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor != nullptr && !actor->IsPendingDestroy() && actor->guid == guid)
            {
                return actor;
            }
        }
        return nullptr;
    }

    std::size_t World::RemoveDestroyedActors()
    {
        // 親(か、その上のどれか)が Destroy 済みのアクターも Destroy する(親を消すと子孫も消える。HIER-2a)。
        // 祖先をすべて調べるので、一覧の順番に関係なく1回で済む。
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            const Actor* ancestor = actor->GetParent();
            for (int depth = 0; ancestor != nullptr && depth < Actor::MaxHierarchyDepth; ++depth)
            {
                if (ancestor->IsPendingDestroy())
                {
                    actor->Destroy();
                    break;
                }
                ancestor = ancestor->GetParent();
            }
        }

        // 先にコンポーネントを片付ける。
        // Destroy 済みのアクターは、そのコンポーネントもまとめて Destroy する(部品はアクターと一緒に消える)。
        // 生きているアクターは、Destroy 済みのコンポーネントを一覧から外す。
        // どちらも FlushDestroyed の前なので、同じフレームの終わりに破棄が確定する。
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor == nullptr)
            {
                continue;
            }
            if (actor->IsPendingDestroy())
            {
                actor->DestroyComponents();
            }
            else
            {
                actor->RemoveDestroyedComponents();
            }
        }

        const std::size_t before = actors.size();
        // Destroy 済みか、もう存在しないアクターのハンドルを、一覧から消す。
        actors.erase(std::remove_if(actors.begin(), actors.end(),
                                    [this](core::ObjectHandle handle) {
                                        const Actor* actor = ResolveActor(handle);
                                        return actor == nullptr || actor->IsPendingDestroy();
                                    }),
                     actors.end());
        return before - actors.size();
    }
}

// ---- シーン(SC-2b。入れ物の木) ----
namespace husky::engine
{
    core::ObjectHandle World::GetRootSceneHandle()
    {
        Scene* root = GetRootScene();
        return root != nullptr ? root->GetHandle() : core::ObjectHandle{};
    }

    Scene* World::GetRootScene()
    {
        core::ObjectRegistry* registry = GetRegistry();
        if (registry == nullptr)
        {
            return nullptr;
        }
        if (Scene* root = registry->Resolve<Scene>(rootScene))
        {
            return root;
        }
        // 最初に呼ばれたときに作る(ワールドのコンストラクタの中では、まだレジストリが付いていないため)。
        Scene* root = registry->Create<Scene>();
        if (root != nullptr)
        {
            rootScene = root->GetHandle();
        }
        return root;
    }

    namespace
    {
        // scene とその子孫から、名前が合うものを探す(深さ優先)。
        Scene* FindSceneIn(core::ObjectRegistry& registry, Scene& scene, std::string_view sceneName, int depth)
        {
            if (scene.name == sceneName)
            {
                return &scene;
            }
            if (depth >= 64)
            {
                return nullptr;
            }
            for (const core::ObjectHandle handle : scene.children)
            {
                Scene* child = registry.Resolve<Scene>(handle);
                if (child != nullptr && !child->IsPendingDestroy())
                {
                    if (Scene* found = FindSceneIn(registry, *child, sceneName, depth + 1))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }
    }

    Scene* World::FindScene(std::string_view sceneName)
    {
        Scene* root = GetRootScene();
        if (root == nullptr || sceneName.empty())
        {
            return nullptr;
        }
        return FindSceneIn(*GetRegistry(), *root, sceneName, 0);
    }

    void World::LoadScene(std::string_view sceneName, Scene* parent)
    {
        SceneRequest request;
        request.load = true;
        request.sceneName = std::string(sceneName);
        request.parent = parent != nullptr ? parent->GetHandle() : core::ObjectHandle{};
        m_sceneRequests.push_back(std::move(request));
    }

    void World::UnloadScene(std::string_view sceneName)
    {
        SceneRequest request;
        request.load = false;
        request.sceneName = std::string(sceneName);
        m_sceneRequests.push_back(std::move(request));
    }

    Scene* World::CreateChildScene(Scene& parent, std::string_view sceneName, const core::Guid& asset)
    {
        core::ObjectRegistry* registry = GetRegistry();
        if (registry == nullptr || registry->Resolve<Scene>(parent.GetHandle()) != &parent)
        {
            return nullptr;
        }
        Scene* scene = registry->Create<Scene>();
        if (scene == nullptr)
        {
            return nullptr;
        }
        scene->name = std::string(sceneName);
        scene->asset = asset;
        scene->m_parent = parent.GetHandle();
        parent.children.push_back(scene->GetHandle()); // 親が強く指すので、回収されない
        return scene;
    }

    bool World::UnloadSceneNow(Scene& scene, std::size_t* outDestroyedActors)
    {
        core::ObjectRegistry* registry = GetRegistry();
        if (registry == nullptr || scene.IsRoot())
        {
            return false;
        }
        // 1. 中のアクター(子孫のシーンの中も)を Destroy する。一覧から外れるのはフレームの終わり。
        std::size_t destroyed = 0;
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor == nullptr || actor->IsPendingDestroy())
            {
                continue;
            }
            const Scene* actorScene = actor->GetScene();
            if (actorScene != nullptr && actorScene->IsSelfOrDescendantOf(scene))
            {
                actor->Destroy();
                ++destroyed;
            }
        }
        // 2. 親から外す(親が指さなくなるので、Destroy と合わせて回収される)。
        if (Scene* parent = scene.GetParent())
        {
            const core::ObjectHandle self = scene.GetHandle();
            parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), self), parent->children.end());
        }
        // 3. このシーンと子孫のシーンを Destroy する(子は親から先に外さなくても、親が消えれば誰も指さない)。
        std::vector<Scene*> stack{&scene};
        while (!stack.empty())
        {
            Scene* current = stack.back();
            stack.pop_back();
            for (const core::ObjectHandle handle : current->children)
            {
                if (Scene* child = registry->Resolve<Scene>(handle))
                {
                    stack.push_back(child);
                }
            }
            current->Destroy();
        }
        if (outDestroyedActors != nullptr)
        {
            *outDestroyedActors = destroyed;
        }
        return true;
    }

    void World::UnloadAllChildScenes()
    {
        m_sceneRequests.clear();
        Scene* root = GetRootScene();
        if (root == nullptr)
        {
            return;
        }
        while (!root->children.empty())
        {
            Scene* child = GetRegistry()->Resolve<Scene>(root->children.back());
            if (child == nullptr)
            {
                root->children.pop_back(); // もう無いものは外すだけ
                continue;
            }
            UnloadSceneNow(*child);
        }
    }

    bool World::MoveToScene(Actor& actor, Scene& scene)
    {
        if (actor.GetWorld() != this)
        {
            return false;
        }
        const Actor* parent = actor.GetParent();
        if (parent != nullptr && parent->m_scene != scene.GetHandle())
        {
            HUSKY_LOG(SceneLog::SceneMoveRefused, actor.name, scene.name);
            return false;
        }
        // 子孫も一緒に移す(親子は同じシーンという決まりを保つ)。
        std::vector<Actor*> descendants;
        actor.GetDescendants(descendants);
        actor.m_scene = scene.GetHandle();
        for (Actor* descendant : descendants)
        {
            descendant->m_scene = scene.GetHandle();
        }
        return true;
    }
}
