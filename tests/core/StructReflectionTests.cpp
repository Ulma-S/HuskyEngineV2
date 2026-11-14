// 構造体のリフレクション(HUSKY_STRUCT_BEGIN)のテスト。
#include "core/ObjectRegistry.h"
#include "core/StructReflection.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using husky::core::IsReflectedStruct;
using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;
using husky::core::Property;
using husky::core::PropertyType;
using husky::core::StructTypeOf;

namespace structtest
{
    struct Vector2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // 登録は、構造体と同じ名前空間の中に書く。
    HUSKY_STRUCT_BEGIN(Vector2)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
    HUSKY_STRUCT_END()

    // 構造体の中に、登録した構造体(Vector2)とハンドルを持つ。中の構造体を先に登録しておく。
    struct Waypoint
    {
        std::string label;
        Vector2 position;
        ObjectHandle owner; // 構造体の中のハンドルも、GC がたどる
    };

    HUSKY_STRUCT_BEGIN(Waypoint)
        HUSKY_PROPERTY(label)
        HUSKY_PROPERTY(position)
        HUSKY_PROPERTY(owner)
    HUSKY_STRUCT_END()

    struct NotRegistered
    {
        int value = 0;
    };

    class Walker : public Object
    {
        HUSKY_OBJECT_INLINE(Walker, Object)
    public:
        Vector2 velocity;
        Waypoint goal;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(velocity)
            HUSKY_PROPERTY(goal)
        HUSKY_REFLECTION_END()
    };
}

using structtest::Vector2;
using structtest::Walker;
using structtest::Waypoint;

TEST_CASE("StructReflection: 登録した構造体だけが、構造体として見分けられる")
{
    CHECK(IsReflectedStruct<Vector2>::value);
    CHECK(IsReflectedStruct<Waypoint>::value);
    CHECK_FALSE(IsReflectedStruct<structtest::NotRegistered>::value);
    CHECK_FALSE(IsReflectedStruct<float>::value);
    CHECK(StructTypeOf<float>() == nullptr);
    CHECK(StructTypeOf<Vector2>() == StructTypeOf<Vector2>()); // 型情報はプログラムに1つだけ
}

TEST_CASE("StructReflection: 構造体の型情報は、名前と、登録した順のメンバーを持つ(入れ子も)")
{
    const husky::core::TypeInfo& waypoint = *StructTypeOf<Waypoint>();
    CHECK(std::string(waypoint.GetName()) == "Waypoint");
    const auto& members = waypoint.GetOwnProperties();
    REQUIRE(members.size() == 3);
    CHECK(members[0].GetType() == PropertyType::String);
    CHECK(members[1].GetType() == PropertyType::Struct);
    CHECK(members[1].GetStructType() == StructTypeOf<Vector2>()); // 入れ子の構造体の型情報を持っている
    CHECK(members[2].GetType() == PropertyType::ObjectHandle);
}

TEST_CASE("StructReflection: オブジェクトの構造体のプロパティを、型を確かめて読み書きできる")
{
    ObjectRegistry registry;
    Walker* walker = registry.Create<Walker>();
    const Property* velocity = Walker::StaticType().FindProperty("velocity");
    REQUIRE(velocity != nullptr);
    CHECK(velocity->GetType() == PropertyType::Struct);

    Vector2* value = velocity->GetValuePtr<Vector2>(*walker);
    REQUIRE(value != nullptr);
    value->x = 3.0f;
    CHECK(walker->velocity.x == 3.0f);
    CHECK(velocity->GetValuePtr<Waypoint>(*walker) == nullptr); // 違う構造体の型では読めない

    // 構造体の中のメンバーは、構造体のアドレスを持ち主として読む。
    const Property* y = StructTypeOf<Vector2>()->FindProperty("y");
    *y->GetValuePtrAt<float>(velocity->GetStructAddressAt(walker)) = 4.5f;
    CHECK(walker->velocity.y == 4.5f);
}

TEST_CASE("StructReflection: 構造体の中のハンドルも、GC がたどる(強い参照)")
{
    ObjectRegistry registry;
    Walker* walker = registry.Create<Walker>();
    registry.AddRoot(walker->GetHandle());
    const ObjectHandle target = registry.Create<Walker>()->GetHandle();
    walker->goal.owner = target; // 構造体(Waypoint)の中のハンドルだけが指している

    CHECK(registry.CollectGarbage() == 0); // 根 → walker → goal.owner とたどれるので、回収されない
    CHECK(registry.Resolve(target) != nullptr);

    walker->goal.owner = ObjectHandle{};
    CHECK(registry.CollectGarbage() == 1);
}
