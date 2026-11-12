// TypeRegistry(型の名前からオブジェクトを作る)のテスト。
#include "core/TypeRegistry.h"

#include <doctest/doctest.h>

using husky::core::Cast;
using husky::core::Object;
using husky::core::ObjectRegistry;
using husky::core::TypeRegistry;

namespace
{
    class Fruit : public Object
    {
        HUSKY_OBJECT(Fruit, Object)
    public:
        int sweetness = 3;
    };

    class Apple : public Fruit
    {
        HUSKY_OBJECT(Apple, Fruit)
    };
}

TEST_CASE("TypeRegistry: 登録した型を、名前で探して作れる")
{
    TypeRegistry types;
    CHECK(types.Register<Fruit>());
    CHECK(types.Register<Apple>());
    CHECK(types.GetCount() == 2);

    CHECK(types.Find("Apple") == &Apple::StaticType());

    ObjectRegistry registry;
    Object* object = types.Create("Apple", registry);
    REQUIRE(object != nullptr);
    CHECK(object->GetType().GetName() == std::string("Apple")); // 本当に Apple として作られた
    CHECK(Cast<Fruit>(object) != nullptr);                     // 親の型としても使える
    CHECK(Cast<Fruit>(object)->sweetness == 3);                // コンストラクタが呼ばれた
    CHECK(registry.Resolve(object->GetHandle()) == object);    // 持ち主はレジストリ
}

TEST_CASE("TypeRegistry: 知らない名前は nullptr。同じ型の2回目の登録は false")
{
    TypeRegistry types;
    types.Register<Fruit>();
    CHECK_FALSE(types.Register<Fruit>());
    CHECK(types.GetCount() == 1);

    ObjectRegistry registry;
    CHECK(types.Find("Banana") == nullptr);
    CHECK(types.Create("Banana", registry) == nullptr);
    CHECK(registry.GetObjectCount() == 0);
}
