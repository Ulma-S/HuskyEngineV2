// ObjectRegistry(オブジェクトの生成と破棄)のテスト。
#include "core/ObjectRegistry.h"

#include <doctest/doctest.h>

#include <vector>

using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;

namespace
{
    // デストラクタが呼ばれた回数を数える。
    int g_destroyedCount = 0;

    class Enemy : public Object
    {
        HUSKY_OBJECT(Enemy, Object)
    public:
        Enemy() = default;
        Enemy(int startHp) : hp(startHp) {} // Create<Enemy>(30) で呼ばれる
        ~Enemy() override { ++g_destroyedCount; }

        int hp = 10;
    };

    class Item : public Object
    {
        HUSKY_OBJECT(Item, Object)
    };
}

TEST_CASE("ObjectRegistry: Create で作ったオブジェクトは、ハンドルから引ける")
{
    ObjectRegistry registry;
    Enemy* enemy = registry.Create<Enemy>();

    REQUIRE(enemy != nullptr);
    CHECK_FALSE(enemy->GetHandle().IsNull());
    CHECK(registry.GetObjectCount() == 1);
    CHECK(registry.Resolve(enemy->GetHandle()) == enemy);
    CHECK(registry.Resolve<Enemy>(enemy->GetHandle()) == enemy);
}

TEST_CASE("ObjectRegistry: Create に渡した引数は、コンストラクタに渡される")
{
    ObjectRegistry registry;
    Enemy* enemy = registry.Create<Enemy>(30);
    CHECK(enemy->hp == 30);
}

TEST_CASE("ObjectRegistry: 型付きの Resolve は、型が違えば nullptr")
{
    ObjectRegistry registry;
    Item* item = registry.Create<Item>();
    CHECK(registry.Resolve<Enemy>(item->GetHandle()) == nullptr);
    CHECK(registry.Resolve<Item>(item->GetHandle()) == item);
}

TEST_CASE("ObjectRegistry: Destroy は予約だけ。FlushDestroyed までは生きている")
{
    g_destroyedCount = 0;
    ObjectRegistry registry;
    Enemy* enemy = registry.Create<Enemy>();
    const ObjectHandle handle = enemy->GetHandle();

    enemy->Destroy();

    // まだ破棄されていない。同じフレームの中なら、メモリもハンドルも有効。
    CHECK(enemy->IsPendingDestroy());
    CHECK(g_destroyedCount == 0);
    CHECK(registry.Resolve(handle) == enemy);
    CHECK(registry.GetObjectCount() == 1);

    // フレームの終わり: ここで実際に破棄される。
    CHECK(registry.FlushDestroyed() == 1);
    CHECK(g_destroyedCount == 1);
    CHECK(registry.Resolve(handle) == nullptr);
    CHECK(registry.GetObjectCount() == 0);
}

TEST_CASE("ObjectRegistry: Destroy を何度呼んでも、破棄は1回だけ")
{
    g_destroyedCount = 0;
    ObjectRegistry registry;
    Enemy* enemy = registry.Create<Enemy>();

    enemy->Destroy();
    enemy->Destroy();
    CHECK(registry.FlushDestroyed() == 1);
    CHECK(g_destroyedCount == 1);

    // 予約は空になっているので、もう一度呼んでも何もしない。
    CHECK(registry.FlushDestroyed() == 0);
}

TEST_CASE("ObjectRegistry: レジストリを破棄すると、残っているオブジェクトも破棄される")
{
    g_destroyedCount = 0;
    {
        ObjectRegistry registry;
        registry.Create<Enemy>();
        registry.Create<Enemy>();
        registry.Create<Item>(); // Item は数えない
    } // ← registry のデストラクタ → 中の unique_ptr → 各オブジェクトのデストラクタ
    CHECK(g_destroyedCount == 2);
}

TEST_CASE("ObjectRegistry: たくさん作っても、先に作ったオブジェクトの場所は動かない")
{
    ObjectRegistry registry;
    Enemy* first = registry.Create<Enemy>();
    const ObjectHandle firstHandle = first->GetHandle();

    // 表(vector)が何度も大きくなる(引っ越す)くらい作る。
    for (int i = 0; i < 1000; ++i)
    {
        registry.Create<Enemy>();
    }

    // 表が引っ越しても、オブジェクト本体は同じ場所にある。
    CHECK(registry.Resolve(firstHandle) == first);
    first->hp = 77; // 古いポインタで触っても安全
    CHECK(registry.Resolve<Enemy>(firstHandle)->hp == 77);
}

TEST_CASE("ObjectRegistry: 破棄した場所が再利用されても、古いハンドルは新しいオブジェクトを指さない")
{
    ObjectRegistry registry;
    Enemy* oldEnemy = registry.Create<Enemy>();
    const ObjectHandle oldHandle = oldEnemy->GetHandle();
    oldEnemy->Destroy();
    registry.FlushDestroyed();

    Enemy* newEnemy = registry.Create<Enemy>();
    CHECK(newEnemy->GetHandle().index == oldHandle.index); // 場所は再利用された
    CHECK(registry.Resolve(oldHandle) == nullptr);             // でも古いハンドルは無効
}

TEST_CASE("Object: レジストリで作っていないオブジェクトの Destroy は何もしない")
{
    Item item; // ローカル変数として作った(レジストリを通していない)
    item.Destroy();
    CHECK_FALSE(item.IsPendingDestroy());
    CHECK(item.GetHandle().IsNull());
}
