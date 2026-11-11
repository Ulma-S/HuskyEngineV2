// リフレクションのプロパティ登録(HUSKY_PROPERTY)のテスト。
#include "core/Object.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::Property;
using husky::core::PropertyType;

namespace
{
    // テスト用のクラス階層:
    //   Object
    //   └ Actor(name, visible)
    //      ├ Enemy(hp, speed, target)
    //      │  └ Boss(rage)
    //      └ Prop(プロパティを登録しない)
    class Actor : public Object
    {
        HUSKY_OBJECT(Actor, Object)
    public:
        std::string name = "actor";
        bool visible = true;

        HUSKY_PROPERTIES_BEGIN()
            HUSKY_PROPERTY(name)
            HUSKY_PROPERTY(visible)
        HUSKY_PROPERTIES_END()
    };

    class Enemy : public Actor
    {
        HUSKY_OBJECT(Enemy, Actor)
    public:
        std::int32_t hp = 10;
        float speed = 1.5f;
        ObjectHandle target;
        int notRegistered = 0; // 登録しないメンバーは、リフレクションからは見えない

        HUSKY_PROPERTIES_BEGIN()
            HUSKY_PROPERTY(hp)
            HUSKY_PROPERTY(speed)
            HUSKY_PROPERTY(target)
        HUSKY_PROPERTIES_END()
    };

    class Boss : public Enemy
    {
        HUSKY_OBJECT(Boss, Enemy)
    public:
        std::int32_t rage = 0;

        HUSKY_PROPERTIES_BEGIN()
            HUSKY_PROPERTY(rage)
        HUSKY_PROPERTIES_END()
    };

    // HUSKY_PROPERTIES_BEGIN を書かないクラス。親のプロパティを二重に登録しないことを確かめる。
    class Prop : public Actor
    {
        HUSKY_OBJECT(Prop, Actor)
    };

    // ForEachProperty で集めた名前の一覧。
    std::vector<std::string> AllPropertyNames(const husky::core::TypeInfo& type)
    {
        std::vector<std::string> names;
        // [&names] はラムダ式が外の names を参照で使う(キャプチャ)という意味。C# のラムダは自動でこうなる。
        type.ForEachProperty([&names](const Property& property) { names.push_back(property.GetName()); });
        return names;
    }
}

TEST_CASE("Property: 登録したプロパティの名前、型、オフセットが記録される")
{
    const auto& properties = Enemy::StaticType().GetOwnProperties();
    REQUIRE(properties.size() == 3); // notRegistered は含まない

    CHECK(std::strcmp(properties[0].GetName(), "hp") == 0);
    CHECK(properties[0].GetType() == PropertyType::Int32);
    CHECK(properties[0].GetOffset() == offsetof(Enemy, hp));

    CHECK(std::strcmp(properties[1].GetName(), "speed") == 0);
    CHECK(properties[1].GetType() == PropertyType::Float);

    CHECK(std::strcmp(properties[2].GetName(), "target") == 0);
    CHECK(properties[2].GetType() == PropertyType::ObjectHandle);
}

TEST_CASE("Property: ほかのオブジェクトへの参照(ObjectHandle)だけが、参照フラグを持つ")
{
    CHECK(Enemy::StaticType().FindProperty("target")->IsReference());
    CHECK_FALSE(Enemy::StaticType().FindProperty("hp")->IsReference());
    CHECK_FALSE(Enemy::StaticType().FindProperty("name")->IsReference());
}

TEST_CASE("Property: FindProperty は親のプロパティも探す")
{
    const auto& type = Boss::StaticType();
    CHECK(type.FindProperty("rage") != nullptr);  // 自分
    CHECK(type.FindProperty("hp") != nullptr);    // 親(Enemy)
    CHECK(type.FindProperty("name") != nullptr);  // 祖父(Actor)
    CHECK(type.FindProperty("nothing") == nullptr);
    CHECK(type.FindProperty("notRegistered") == nullptr);
}

TEST_CASE("Property: ForEachProperty は、一番上の親から順にすべてのプロパティを回る")
{
    const std::vector<std::string> expected = {"name", "visible", "hp", "speed", "target", "rage"};
    CHECK(AllPropertyNames(Boss::StaticType()) == expected);
}

TEST_CASE("Property: プロパティを登録しないクラスは、親のプロパティを二重に持たない")
{
    CHECK(Prop::StaticType().GetOwnProperties().empty());
    const std::vector<std::string> expected = {"name", "visible"};
    CHECK(AllPropertyNames(Prop::StaticType()) == expected);
}

TEST_CASE("Property: プロパティを通して、オブジェクトの値を読み書きできる")
{
    Boss boss;
    boss.hp = 50;

    // Object& として渡す(型を知らなくても、プロパティの情報だけで値に届く)。
    Object& object = boss;

    const Property* hpProperty = Boss::StaticType().FindProperty("hp");
    REQUIRE(hpProperty != nullptr);
    std::int32_t* hp = hpProperty->GetValuePtr<std::int32_t>(object);
    REQUIRE(hp != nullptr);
    CHECK(*hp == 50);

    *hp = 7; // プロパティ経由で書き換える
    CHECK(boss.hp == 7);

    std::string* name = Boss::StaticType().FindProperty("name")->GetValuePtr<std::string>(object);
    REQUIRE(name != nullptr);
    *name = "ドラゴン";
    CHECK(boss.name == "ドラゴン");
}

TEST_CASE("Property: 型を間違えて読もうとすると nullptr(メモリを壊さない)")
{
    Enemy enemy;
    const Property* hpProperty = Enemy::StaticType().FindProperty("hp");
    CHECK(hpProperty->GetValuePtr<float>(enemy) == nullptr);
    CHECK(hpProperty->GetValuePtr<std::string>(enemy) == nullptr);
}
