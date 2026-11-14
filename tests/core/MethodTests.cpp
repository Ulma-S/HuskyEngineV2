// リフレクションのメソッド登録(HUSKY_METHOD)と、名前と値の配列による呼び出しのテスト。
#include "core/Object.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using husky::core::Method;
using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::PropertyType;
using husky::core::Value;

namespace
{
    // テスト用のクラス階層:
    //   Object
    //   └ Character(Heal, GetHp, IsAlive)
    //      └ Wizard(Greet, Scale, SetTarget, GetTarget)
    //   Object
    //   └ Rock(メソッドなし。違う型で呼ぶテスト用)
    class Character : public Object
    {
        HUSKY_OBJECT_INLINE(Character, Object)
    public:
        std::int32_t hp = 10;

        void Heal(std::int32_t amount) { hp += amount; }
        std::int32_t GetHp() const { return hp; }
        bool IsAlive() const { return hp > 0; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(hp)
            HUSKY_METHOD(Heal)
            HUSKY_METHOD(GetHp)
            HUSKY_METHOD(IsAlive)
        HUSKY_REFLECTION_END()
    };

    class Wizard : public Character
    {
        HUSKY_OBJECT_INLINE(Wizard, Character)
    public:
        ObjectHandle target;

        // const std::string& で受け取る関数も登録できる(内部では std::string として扱う)。
        std::string Greet(const std::string& name) const { return "こんにちは、" + name; }
        float Scale(float value, std::int32_t times) const { return value * static_cast<float>(times); }
        void SetTarget(ObjectHandle handle) { target = handle; }
        ObjectHandle GetTarget() const { return target; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_METHOD(Greet)
            HUSKY_METHOD(Scale)
            HUSKY_METHOD(SetTarget)
            HUSKY_METHOD(GetTarget)
        HUSKY_REFLECTION_END()
    };

    class Rock : public Object
    {
        HUSKY_OBJECT_INLINE(Rock, Object)
    };
}

TEST_CASE("Method: 登録したメソッドの名前、引数の型、戻り値の型が記録される")
{
    const Method* heal = Character::StaticType().FindMethod("Heal");
    REQUIRE(heal != nullptr);
    CHECK(std::strcmp(heal->GetName(), "Heal") == 0);
    CHECK(heal->GetParameterTypes() == std::vector<PropertyType>{PropertyType::Int32});
    CHECK_FALSE(heal->GetReturnType().has_value()); // void

    const Method* scale = Wizard::StaticType().FindMethod("Scale");
    REQUIRE(scale != nullptr);
    CHECK(scale->GetParameterTypes() == std::vector<PropertyType>{PropertyType::Float, PropertyType::Int32});
    CHECK(scale->GetReturnType() == PropertyType::Float);

    CHECK(Character::StaticType().FindMethod("Nothing") == nullptr);
}

TEST_CASE("Method: 名前と値の配列で呼び出せる(戻り値なし)")
{
    Character character;
    Object& object = character; // 型を知らない呼び出し側を想定して、Object& として扱う

    Value result;
    CHECK(Character::StaticType().FindMethod("Heal")->Invoke(object, {std::int32_t{5}}, result));
    CHECK(character.hp == 15);
    CHECK(std::holds_alternative<std::monostate>(result)); // 戻り値なしは空
}

TEST_CASE("Method: 戻り値は Value に入って返る")
{
    Character character;
    character.hp = 42;

    Value result;
    REQUIRE(Character::StaticType().FindMethod("GetHp")->Invoke(character, {}, result));
    REQUIRE(std::get_if<std::int32_t>(&result) != nullptr); // get_if: 型が合えば中身へのポインタ、違えば nullptr
    CHECK(*std::get_if<std::int32_t>(&result) == 42);

    REQUIRE(Character::StaticType().FindMethod("IsAlive")->Invoke(character, {}, result));
    CHECK(*std::get_if<bool>(&result) == true);
}

TEST_CASE("Method: 文字列、小数、複数の引数、ハンドルも扱える")
{
    Wizard wizard;
    Value result;

    REQUIRE(Wizard::StaticType().FindMethod("Greet")->Invoke(wizard, {std::string("ハスキー")}, result));
    CHECK(*std::get_if<std::string>(&result) == "こんにちは、ハスキー");

    REQUIRE(Wizard::StaticType().FindMethod("Scale")->Invoke(wizard, {1.5f, std::int32_t{4}}, result));
    CHECK(*std::get_if<float>(&result) == doctest::Approx(6.0f));

    const ObjectHandle handle{3, 7};
    REQUIRE(Wizard::StaticType().FindMethod("SetTarget")->Invoke(wizard, {handle}, result));
    CHECK(wizard.target == handle);
    REQUIRE(Wizard::StaticType().FindMethod("GetTarget")->Invoke(wizard, {}, result));
    CHECK(*std::get_if<ObjectHandle>(&result) == handle);
}

TEST_CASE("Method: 引数の数が違えば、呼ばずに false")
{
    Character character;
    Value result;
    const Method* heal = Character::StaticType().FindMethod("Heal");
    CHECK_FALSE(heal->Invoke(character, {}, result));
    CHECK_FALSE(heal->Invoke(character, {std::int32_t{1}, std::int32_t{2}}, result));
    CHECK(character.hp == 10); // 呼ばれていない
}

TEST_CASE("Method: 引数の型が違えば、呼ばずに false")
{
    Character character;
    Value result;
    const Method* heal = Character::StaticType().FindMethod("Heal");
    CHECK_FALSE(heal->Invoke(character, {1.5f}, result));                // float は int32 ではない
    CHECK_FALSE(heal->Invoke(character, {std::string("5")}, result));    // 文字列も不可
    CHECK(character.hp == 10);
}

TEST_CASE("Method: メソッドを持たない型のオブジェクトで呼ぶと、呼ばずに false")
{
    Rock rock;
    Value result;
    CHECK_FALSE(Character::StaticType().FindMethod("Heal")->Invoke(rock, {std::int32_t{5}}, result));
}

TEST_CASE("Method: 親クラスで登録したメソッドは、子クラスから探せて、子のオブジェクトで呼べる")
{
    Wizard wizard;
    const Method* heal = Wizard::StaticType().FindMethod("Heal"); // Heal は Character で登録
    REQUIRE(heal != nullptr);
    CHECK(heal->GetOwner() == &Character::StaticType());

    Value result;
    CHECK(heal->Invoke(wizard, {std::int32_t{3}}, result));
    CHECK(wizard.hp == 13);
}

TEST_CASE("Method: プロパティとメソッドは、同じ登録ブロックで両方登録できる")
{
    CHECK(Character::StaticType().GetOwnProperties().size() == 1);
    CHECK(Character::StaticType().GetOwnMethods().size() == 3);
    CHECK(Wizard::StaticType().GetOwnMethods().size() == 4); // 親の分は含まない
}
