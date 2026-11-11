// ObjectHeap(オブジェクトの生成と破棄)のテスト。
#include "core/ObjectHeap.h"

#include <doctest/doctest.h>

#include <vector>

using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::ObjectHeap;

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

TEST_CASE("ObjectHeap: Create で作ったオブジェクトは、ハンドルから引ける")
{
    ObjectHeap heap;
    Enemy* enemy = heap.Create<Enemy>();

    REQUIRE(enemy != nullptr);
    CHECK_FALSE(enemy->GetHandle().IsNull());
    CHECK(heap.GetObjectCount() == 1);
    CHECK(heap.Resolve(enemy->GetHandle()) == enemy);
    CHECK(heap.Resolve<Enemy>(enemy->GetHandle()) == enemy);
}

TEST_CASE("ObjectHeap: Create に渡した引数は、コンストラクタに渡される")
{
    ObjectHeap heap;
    Enemy* enemy = heap.Create<Enemy>(30);
    CHECK(enemy->hp == 30);
}

TEST_CASE("ObjectHeap: 型付きの Resolve は、型が違えば nullptr")
{
    ObjectHeap heap;
    Item* item = heap.Create<Item>();
    CHECK(heap.Resolve<Enemy>(item->GetHandle()) == nullptr);
    CHECK(heap.Resolve<Item>(item->GetHandle()) == item);
}

TEST_CASE("ObjectHeap: Destroy は予約だけ。FlushDestroyed までは生きている")
{
    g_destroyedCount = 0;
    ObjectHeap heap;
    Enemy* enemy = heap.Create<Enemy>();
    const ObjectHandle handle = enemy->GetHandle();

    enemy->Destroy();

    // まだ破棄されていない。同じフレームの中なら、メモリもハンドルも有効。
    CHECK(enemy->IsPendingDestroy());
    CHECK(g_destroyedCount == 0);
    CHECK(heap.Resolve(handle) == enemy);
    CHECK(heap.GetObjectCount() == 1);

    // フレームの終わり: ここで実際に破棄される。
    CHECK(heap.FlushDestroyed() == 1);
    CHECK(g_destroyedCount == 1);
    CHECK(heap.Resolve(handle) == nullptr);
    CHECK(heap.GetObjectCount() == 0);
}

TEST_CASE("ObjectHeap: Destroy を何度呼んでも、破棄は1回だけ")
{
    g_destroyedCount = 0;
    ObjectHeap heap;
    Enemy* enemy = heap.Create<Enemy>();

    enemy->Destroy();
    enemy->Destroy();
    CHECK(heap.FlushDestroyed() == 1);
    CHECK(g_destroyedCount == 1);

    // 予約は空になっているので、もう一度呼んでも何もしない。
    CHECK(heap.FlushDestroyed() == 0);
}

TEST_CASE("ObjectHeap: ヒープを破棄すると、残っているオブジェクトも破棄される")
{
    g_destroyedCount = 0;
    {
        ObjectHeap heap;
        heap.Create<Enemy>();
        heap.Create<Enemy>();
        heap.Create<Item>(); // Item は数えない
    } // ← heap のデストラクタ → 中の unique_ptr → 各オブジェクトのデストラクタ
    CHECK(g_destroyedCount == 2);
}

TEST_CASE("ObjectHeap: たくさん作っても、先に作ったオブジェクトの場所は動かない")
{
    ObjectHeap heap;
    Enemy* first = heap.Create<Enemy>();
    const ObjectHandle firstHandle = first->GetHandle();

    // 表(vector)が何度も大きくなる(引っ越す)くらい作る。
    for (int i = 0; i < 1000; ++i)
    {
        heap.Create<Enemy>();
    }

    // 表が引っ越しても、オブジェクト本体は同じ場所にある。
    CHECK(heap.Resolve(firstHandle) == first);
    first->hp = 77; // 古いポインタで触っても安全
    CHECK(heap.Resolve<Enemy>(firstHandle)->hp == 77);
}

TEST_CASE("ObjectHeap: 破棄した場所が再利用されても、古いハンドルは新しいオブジェクトを指さない")
{
    ObjectHeap heap;
    Enemy* oldEnemy = heap.Create<Enemy>();
    const ObjectHandle oldHandle = oldEnemy->GetHandle();
    oldEnemy->Destroy();
    heap.FlushDestroyed();

    Enemy* newEnemy = heap.Create<Enemy>();
    CHECK(newEnemy->GetHandle().index == oldHandle.index); // 場所は再利用された
    CHECK(heap.Resolve(oldHandle) == nullptr);             // でも古いハンドルは無効
}

TEST_CASE("Object: ヒープで作っていないオブジェクトの Destroy は何もしない")
{
    Item item; // ローカル変数として作った(ヒープを通していない)
    item.Destroy();
    CHECK_FALSE(item.IsPendingDestroy());
    CHECK(item.GetHandle().IsNull());
}
