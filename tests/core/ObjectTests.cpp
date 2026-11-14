// Object と TypeInfo、HUSKY_OBJECT マクロ、Cast のテスト。
#include "core/Object.h"

#include <doctest/doctest.h>

#include <cstring> // std::strcmp

using husky::core::Cast;
using husky::core::Object;
using husky::core::TypeInfo;

namespace
{
    // テスト用のクラス階層:
    //   Object
    //   └ Actor
    //      ├ Enemy
    //      │  └ Boss
    //      └ Player
    class Actor : public Object
    {
        HUSKY_OBJECT_INLINE(Actor, Object)
    public:
        int x = 0;
    };

    class Enemy : public Actor
    {
        HUSKY_OBJECT_INLINE(Enemy, Actor)
    public:
        int hp = 10;
    };

    class Boss : public Enemy
    {
        HUSKY_OBJECT_INLINE(Boss, Enemy)
    };

    class Player : public Actor
    {
        HUSKY_OBJECT_INLINE(Player, Actor)
    };

    // デストラクタが呼ばれたかを数えるためのクラス。
    int g_destroyedCount = 0;
    class Counted : public Object
    {
        HUSKY_OBJECT_INLINE(Counted, Object)
    public:
        ~Counted() override { ++g_destroyedCount; }
    };
}

TEST_CASE("TypeInfo: 型の名前と親が登録されている")
{
    CHECK(std::strcmp(Enemy::StaticType().GetName(), "Enemy") == 0); // strcmp: C の文字列の比較。等しければ 0
    CHECK(Enemy::StaticType().GetParent() == &Actor::StaticType());
    CHECK(Object::StaticType().GetParent() == nullptr);
}

TEST_CASE("TypeInfo: 型情報は型ごとに1つだけ(何度呼んでも同じアドレス)")
{
    CHECK(&Enemy::StaticType() == &Enemy::StaticType());
    CHECK(&Enemy::StaticType() != &Player::StaticType());
}

TEST_CASE("IsA: 自分自身と、親・祖先の型には true、それ以外には false")
{
    Boss boss;
    CHECK(boss.IsA<Boss>());
    CHECK(boss.IsA<Enemy>());
    CHECK(boss.IsA<Actor>());
    CHECK(boss.IsA<Object>());
    CHECK_FALSE(boss.IsA<Player>());

    Enemy enemy;
    CHECK_FALSE(enemy.IsA<Boss>()); // 親は子の一種ではない
}

TEST_CASE("GetType: 基底クラスのポインタから呼んでも、実際の型が返る")
{
    Boss boss;
    Object* object = &boss; // & はアドレス(ポインタ)を取る演算子
    CHECK(&object->GetType() == &Boss::StaticType());
    CHECK(std::strcmp(object->GetType().GetName(), "Boss") == 0);
}

TEST_CASE("Cast: 正しい型なら変換でき、違う型なら nullptr")
{
    Enemy enemy;
    Object* object = &enemy;

    Enemy* asEnemy = Cast<Enemy>(object);
    REQUIRE(asEnemy != nullptr);
    asEnemy->hp = 3;
    CHECK(enemy.hp == 3); // 同じオブジェクトを指している

    CHECK(Cast<Actor>(object) != nullptr);  // 親の型にも変換できる
    CHECK(Cast<Player>(object) == nullptr); // 兄弟の型には変換できない
    CHECK(Cast<Boss>(object) == nullptr);   // 子の型には変換できない

    Object* nothing = nullptr;
    CHECK(Cast<Enemy>(nothing) == nullptr);

    const Object* constObject = &enemy;
    const Enemy* constEnemy = Cast<Enemy>(constObject);
    CHECK(constEnemy != nullptr);
}

TEST_CASE("Object: 基底クラスのデストラクタが virtual なので、子のデストラクタも呼ばれる")
{
    g_destroyedCount = 0;
    {
        Counted counted;
        Object& asObject = counted; // 基底クラスの参照として持つ
        (void)asObject;             // 使わない変数の警告を消す
    } // ← ここで counted が破棄され、~Counted が呼ばれる
    CHECK(g_destroyedCount == 1);
}
