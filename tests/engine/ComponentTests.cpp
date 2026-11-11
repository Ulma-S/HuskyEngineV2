// Component と、Actor への取り付けのテスト。dt は固定値を渡すので、結果は毎回同じになる。
#include "engine/Scene.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::ObjectRegistry;
using husky::engine::Actor;
using husky::engine::Component;
using husky::engine::Scene;

namespace
{
    // 呼ばれた順番を書き込む先。テストごとに clear する。
    std::string g_log;

    // Tick で "A" と書くアクター。
    class LoggingActor : public Actor
    {
        HUSKY_OBJECT(LoggingActor, Actor)
    public:
        void Tick(double) override { g_log += "A"; }
    };

    // Tick で "C" と書き、受け取った dt の合計を数えるコンポーネント。
    class Counter : public Component
    {
        HUSKY_OBJECT(Counter, Component)
    public:
        void Tick(double dt) override
        {
            g_log += "C";
            totalTime += dt;
        }
        double totalTime = 0.0;
    };

    // Counter とは別の型(GetComponent の型の区別を確かめる用)。
    class Other : public Component
    {
        HUSKY_OBJECT(Other, Component)
    };

    // Counter の子(GetComponent が子孫も見つけることを確かめる用)。
    class FastCounter : public Counter
    {
        HUSKY_OBJECT(FastCounter, Counter)
    };

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

TEST_CASE("Component: AddComponent で取り付けると、持ち主が分かり、型で探せる")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    Counter* counter = actor->AddComponent<Counter>();

    REQUIRE(counter != nullptr);
    CHECK(counter->GetOwner() == actor);
    CHECK(actor->components.size() == 1);
    CHECK(actor->GetComponent<Counter>() == counter);
    CHECK(actor->GetComponent<Component>() == counter); // 親の型でも見つかる
    CHECK(actor->GetComponent<Other>() == nullptr);     // 違う型は見つからない
}

TEST_CASE("Component: GetComponent は子孫の型も見つけ、一覧で最初のものを返す")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    Other* other = actor->AddComponent<Other>();
    FastCounter* fast = actor->AddComponent<FastCounter>();
    actor->AddComponent<Counter>();

    CHECK(actor->GetComponent<Counter>() == fast);
    CHECK(actor->GetComponent<Component>() == other);
}

TEST_CASE("Component: Scene::Tick は、アクターの Tick の直後に、そのアクターのコンポーネントの Tick を呼ぶ")
{
    g_log.clear();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    LoggingActor* first = scene->Spawn<LoggingActor>();
    first->AddComponent<Counter>();
    first->AddComponent<Counter>();
    LoggingActor* second = scene->Spawn<LoggingActor>();
    Counter* counter = second->AddComponent<Counter>();

    scene->Tick(0.5);
    scene->Tick(0.25);

    CHECK(g_log == "ACCAC" "ACCAC"); // 1フレームに「アクター1、その部品2つ、アクター2、その部品1つ」
    CHECK(counter->totalTime == doctest::Approx(0.75));
}

TEST_CASE("Component: Destroy したコンポーネントは更新されず、フレームの終わりに一覧から外れて消える")
{
    g_log.clear();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    Counter* counter = actor->AddComponent<Counter>();
    const auto counterHandle = counter->GetHandle();

    counter->Destroy();
    CHECK(actor->GetComponent<Counter>() == nullptr); // 予約した時点で、もう見つからない
    scene->Tick(1.0);
    CHECK(g_log.empty());

    EndFrame(registry, *scene);
    CHECK(actor->components.empty());
    CHECK(registry.Resolve(counterHandle) == nullptr);
    CHECK(registry.Resolve(actor->GetHandle()) == actor); // アクターは残る
}

TEST_CASE("Component: アクターを Destroy すると、そのコンポーネントも同じフレームの終わりに消える")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    const auto counterHandle = actor->AddComponent<Counter>()->GetHandle();
    const auto otherHandle = actor->AddComponent<Other>()->GetHandle();
    CHECK(registry.GetObjectCount() == 4); // シーン、アクター、コンポーネント2つ

    actor->Destroy();
    EndFrame(registry, *scene);

    CHECK(registry.Resolve(counterHandle) == nullptr);
    CHECK(registry.Resolve(otherHandle) == nullptr);
    CHECK(registry.GetObjectCount() == 1); // シーンだけ
}

TEST_CASE("Component: シーンにいるアクターのコンポーネントは GC で回収されない。アクターごと届かなくなると回収される")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    Counter* counter = actor->AddComponent<Counter>();
    const auto counterHandle = counter->GetHandle();

    CHECK(registry.CollectGarbage() == 0); // シーン → アクター → コンポーネント とたどれる

    scene->actors.clear(); // アクターを一覧から外す(どこからも届かなくなる)
    CHECK(registry.CollectGarbage() == 2); // アクターとコンポーネント
    CHECK(registry.Resolve(counterHandle) == nullptr);
}

TEST_CASE("Component: コンポーネントは、取り付け先のアクターを生かしておかない(弱い参照)")
{
    ObjectRegistry registry;
    Actor* actor = registry.Create<Actor>(); // シーンにいない(根から届かない)アクター
    Counter* counter = actor->AddComponent<Counter>();
    registry.AddRoot(counter->GetHandle()); // コンポーネントだけを根にする

    CHECK(registry.CollectGarbage() == 1); // アクターは回収される
    CHECK(counter->GetOwner() == nullptr); // 持ち主はもういない、と分かる
}
