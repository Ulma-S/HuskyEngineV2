// Component と、Actor への取り付けのテスト。dt は固定値を渡すので、結果は毎回同じになる。
#include "engine/World.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::ObjectRegistry;
using husky::engine::Actor;
using husky::engine::Component;
using husky::engine::World;
namespace TickOrder = husky::engine::TickOrder;

namespace
{
    // 呼ばれた順番を書き込む先。テストごとに clear する。
    std::string g_log;

    // Tick で "A" と書くアクター。
    class LoggingActor : public Actor
    {
        HUSKY_OBJECT_INLINE(LoggingActor, Actor)
    public:
        void Tick(double) override { g_log += "A"; }
    };

    // Tick で "C" と書き、受け取った dt の合計を数えるコンポーネント。
    class Counter : public Component
    {
        HUSKY_OBJECT_INLINE(Counter, Component)
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
        HUSKY_OBJECT_INLINE(Other, Component)
    };

    // Counter の子(GetComponent が子孫も見つけることを確かめる用)。
    class FastCounter : public Counter
    {
        HUSKY_OBJECT_INLINE(FastCounter, Counter)
    };

    // 実行順ごとに、決まった1文字を書くコンポーネント。
    class InputLogger : public Component
    {
        HUSKY_OBJECT_INLINE(InputLogger, Component)
    public:
        std::int32_t GetTickOrder() const override { return TickOrder::Input; }
        void Tick(double) override { g_log += "i"; }
    };

    class LateLogger : public Component
    {
        HUSKY_OBJECT_INLINE(LateLogger, Component)
    public:
        std::int32_t GetTickOrder() const override { return TickOrder::Late; }
        void Tick(double) override { g_log += "l"; }
    };

    class PreRenderLogger : public Component
    {
        HUSKY_OBJECT_INLINE(PreRenderLogger, Component)
    public:
        std::int32_t GetTickOrder() const override { return TickOrder::PreRender; }
        void Tick(double) override { g_log += "r"; }
    };

    // 名前付きの値の「間」(Default と Late の間)で動く。
    class BetweenLogger : public Component
    {
        HUSKY_OBJECT_INLINE(BetweenLogger, Component)
    public:
        std::int32_t GetTickOrder() const override { return 500; }
        void Tick(double) override { g_log += "b"; }
    };

    // Default の実行順で、持ち主に LateLogger を1つ足す(1回だけ)。
    class LateAdder : public Component
    {
        HUSKY_OBJECT_INLINE(LateAdder, Component)
    public:
        void Tick(double) override
        {
            if (!added)
            {
                GetOwner()->AddComponent<LateLogger>();
                added = true;
            }
        }
        bool added = false;
    };

    // Default の実行順で、持ち主のアクターを Destroy する。
    class OwnerDestroyer : public Component
    {
        HUSKY_OBJECT_INLINE(OwnerDestroyer, Component)
    public:
        void Tick(double) override { GetOwner()->Destroy(); }
    };

    World* CreateRootScene(ObjectRegistry& registry)
    {
        World* scene = registry.Create<World>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    // フレームの終わりの処理(ゲームループと同じ順番)。
    void EndFrame(ObjectRegistry& registry, World& scene)
    {
        scene.RemoveDestroyedActors();
        registry.FlushDestroyed();
    }
}

TEST_CASE("Component: AddComponent で取り付けると、持ち主が分かり、型で探せる")
{
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
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
    World* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    Other* other = actor->AddComponent<Other>();
    FastCounter* fast = actor->AddComponent<FastCounter>();
    actor->AddComponent<Counter>();

    CHECK(actor->GetComponent<Counter>() == fast);
    CHECK(actor->GetComponent<Component>() == other);
}

TEST_CASE("Component: World::Tick は、アクターの Tick の直後に、そのアクターのコンポーネントの Tick を呼ぶ")
{
    g_log.clear();
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
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
    World* scene = CreateRootScene(registry);
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
    World* scene = CreateRootScene(registry);
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
    World* scene = CreateRootScene(registry);
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

TEST_CASE("TickOrder: コンポーネントの既定の実行順は Default(0)")
{
    Counter counter; // レジストリで作らなくても、実行順を聞くだけならできる
    CHECK(counter.GetTickOrder() == TickOrder::Default);
    CHECK(counter.GetTickOrder() == 0);
}

TEST_CASE("TickOrder: World::Tick は、実行順の小さい順に、全アクターを回す")
{
    g_log.clear();
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);

    // 一覧の順番は実行順とは関係ない、ということも確かめるため、わざとばらばらの順に付ける。
    LoggingActor* first = scene->Spawn<LoggingActor>();
    first->AddComponent<PreRenderLogger>();
    first->AddComponent<LateLogger>();
    first->AddComponent<Counter>();
    first->AddComponent<InputLogger>();
    LoggingActor* second = scene->Spawn<LoggingActor>();
    second->AddComponent<LateLogger>();
    second->AddComponent<InputLogger>();

    scene->Tick(1.0);

    // Input(-1000): i i / Default(0): アクター1(A)とその Counter(C)、アクター2(A) / Late(1000): l l / PreRender(2000): r
    CHECK(g_log == "ii" "ACA" "ll" "r");
}

TEST_CASE("TickOrder: 名前付きの値の間の実行順も使える")
{
    g_log.clear();
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    actor->AddComponent<LateLogger>();    // 1000
    actor->AddComponent<BetweenLogger>(); // 500
    actor->AddComponent<Counter>();       // 0

    scene->Tick(1.0);

    CHECK(g_log == "Cbl");
}

TEST_CASE("TickOrder: フレームの途中で足したコンポーネントは、実行順が後でも次のフレームまで更新されない")
{
    g_log.clear();
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    actor->AddComponent<LateAdder>(); // Default で LateLogger を足す

    scene->Tick(1.0);
    CHECK(g_log.empty()); // 足したのはフレームの途中なので、同じフレームの Late では呼ばれない
    CHECK(actor->GetComponent<LateLogger>() != nullptr);

    scene->Tick(1.0);
    CHECK(g_log == "l"); // 次のフレームからは呼ばれる
}

TEST_CASE("TickOrder: Tick の途中でアクターが Destroy されたら、そのアクターの残りのコンポーネントは更新されない")
{
    g_log.clear();
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    Actor* actor = scene->Spawn<Actor>();
    actor->AddComponent<InputLogger>();
    actor->AddComponent<OwnerDestroyer>();
    actor->AddComponent<Counter>();    // 同じ Default でも、Destroy の後なので呼ばれない
    actor->AddComponent<LateLogger>(); // 後の実行順も呼ばれない
    LoggingActor* other = scene->Spawn<LoggingActor>();
    other->AddComponent<LateLogger>(); // ほかのアクターは影響を受けない

    scene->Tick(1.0);

    CHECK(g_log == "i" "A" "l"); // Destroy より前の Input と、ほかのアクターの分だけ
    CHECK(actor->IsPendingDestroy());
}
