// Scene と Actor のテスト。dt は固定値を渡すので、結果は毎回同じになる。
#include "engine/Scene.h"

#include <doctest/doctest.h>

#include <vector>

using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;
using husky::engine::Actor;
using husky::engine::Scene;

namespace
{
    // 毎フレーム右へ動くアクター。Tick が呼ばれた回数も数える。
    class Mover : public Actor
    {
        HUSKY_OBJECT(Mover, Actor)
    public:
        void Tick(double dt) override
        {
            x += static_cast<float>(100.0 * dt); // 秒速 100
            ++tickCount;
        }
        int tickCount = 0;
    };

    // Tick の中で、新しいアクターを1つ作る(弾を撃つようなもの)。
    class Spawner : public Actor
    {
        HUSKY_OBJECT(Spawner, Actor)
    public:
        explicit Spawner(Scene& owner) : scene(&owner) {}
        void Tick(double) override { scene->Spawn<Mover>(); }
        Scene* scene;
    };

    // Tick の中で、自分を Destroy する。
    class SelfDestroyer : public Actor
    {
        HUSKY_OBJECT(SelfDestroyer, Actor)
    public:
        void Tick(double) override { Destroy(); }
    };

    // シーンを作って根にするまでの、毎回同じ準備。
    Scene* CreateRootScene(ObjectRegistry& registry)
    {
        Scene* scene = registry.Create<Scene>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    // フレームの終わりの処理(ゲームループと同じ順番)。
    void EndFrame(ObjectRegistry& registry, Scene& scene)
    {
        scene.RemoveDestroyedActors();
        registry.FlushDestroyed();
    }
}

TEST_CASE("Scene: Spawn したアクターは一覧に入り、シーンが根なら GC で回収されない")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Mover* mover = scene->Spawn<Mover>();

    REQUIRE(mover != nullptr);
    CHECK(scene->actors.size() == 1);
    CHECK(registry.CollectGarbage() == 0);
    CHECK(registry.Resolve(mover->GetHandle()) == mover);
}

TEST_CASE("Scene: Tick は全アクターの Tick を、渡した dt で呼ぶ")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Mover* a = scene->Spawn<Mover>();
    Mover* b = scene->Spawn<Mover>();

    for (int frame = 0; frame < 60; ++frame)
    {
        scene->Tick(1.0 / 60.0); // 固定の dt で1秒分
    }
    CHECK(a->tickCount == 60);
    CHECK(b->x == doctest::Approx(100.0f)); // 秒速 100 で1秒
}

TEST_CASE("Scene: Destroy したアクターは、そのフレームの残りでは更新されず、フレームの終わりに消える")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Mover* mover = scene->Spawn<Mover>();
    const ObjectHandle handle = mover->GetHandle();

    mover->Destroy();
    scene->Tick(0.1);
    CHECK(mover->tickCount == 0); // Destroy 済みなので呼ばれない(メモリはまだ有効)

    EndFrame(registry, *scene);
    CHECK(scene->actors.empty());
    CHECK(registry.Resolve(handle) == nullptr);
}

TEST_CASE("Scene: Tick の中で自分を Destroy しても大丈夫")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    scene->Spawn<SelfDestroyer>();
    scene->Spawn<Mover>();

    scene->Tick(0.1);
    EndFrame(registry, *scene);
    CHECK(scene->actors.size() == 1); // Mover だけが残る
}

TEST_CASE("Scene: Tick の中で Spawn したアクターは、次のフレームから更新される")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    scene->Spawn<Spawner>(*scene);

    scene->Tick(0.1); // Spawner が Mover を1つ作る(作られた Mover は、このフレームでは更新されない)
    REQUIRE(scene->actors.size() == 2);
    Mover* first = registry.Resolve<Mover>(scene->actors[1]);
    REQUIRE(first != nullptr);
    CHECK(first->tickCount == 0);

    // 何度も Spawn して actors が何度も引っ越しても、更新は壊れない。
    for (int frame = 0; frame < 100; ++frame)
    {
        scene->Tick(0.1);
    }
    CHECK(scene->actors.size() == 102);
    CHECK(first->tickCount == 100);
}

TEST_CASE("Scene: 一覧から外したアクターは、次の GC で回収される")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    const ObjectHandle handle = scene->Spawn<Mover>()->GetHandle();

    scene->actors.clear(); // Destroy せずに一覧から外すだけ
    CHECK(registry.CollectGarbage() == 1);
    CHECK(registry.Resolve(handle) == nullptr);
}

TEST_CASE("Scene: ForEachActor は、生きているアクターだけを一覧の順に渡す")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Mover* a = scene->Spawn<Mover>();
    Mover* b = scene->Spawn<Mover>();
    Mover* c = scene->Spawn<Mover>();
    b->Destroy();

    std::vector<Actor*> visited;
    scene->ForEachActor([&visited](Actor& actor) { visited.push_back(&actor); });
    CHECK(visited == std::vector<Actor*>{a, c});
}

TEST_CASE("Actor: 名前と位置は、リフレクションのプロパティとして見える")
{
    CHECK(Actor::StaticType().FindProperty("name") != nullptr);
    CHECK(Actor::StaticType().FindProperty("x") != nullptr);
    CHECK(Mover::StaticType().FindProperty("y") != nullptr); // 子クラスからも見える
}
