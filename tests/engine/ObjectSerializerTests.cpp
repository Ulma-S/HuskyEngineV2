// ObjectSerializer(リフレクションでオブジェクト ⇄ ArchiveValue)のテスト。
#include "engine/Actor.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;
using husky::engine::Actor;
using husky::engine::ArchiveValue;
namespace JsonFormat = husky::engine::JsonFormat;
namespace ObjectSerializer = husky::engine::ObjectSerializer;

namespace
{
    // プロパティにできる型を一通り持つアクター。Actor の name、x、y も引き継ぐ。
    class SaveTestEnemy : public Actor
    {
        HUSKY_OBJECT(SaveTestEnemy, Actor)
    public:
        bool angry = false;
        std::int32_t hp = 10;
        float speed = 1.0f;
        std::string title = "なし";
        ObjectHandle target;                     // ハンドル(今は保存しない)
        std::vector<ObjectHandle> followers;     // ハンドルの配列(今は保存しない)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(angry)
            HUSKY_PROPERTY(hp)
            HUSKY_PROPERTY(speed)
            HUSKY_PROPERTY(title)
            HUSKY_PROPERTY(target)
            HUSKY_PROPERTY(followers)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("ObjectSerializer: 型の名前と、親クラスから順に値のプロパティを書く。ハンドルは書かない")
{
    ObjectRegistry registry;
    SaveTestEnemy* enemy = registry.Create<SaveTestEnemy>();
    enemy->name = "スライム";
    enemy->x = 12.5f;
    enemy->hp = -3;
    enemy->angry = true;

    const ArchiveValue saved = ObjectSerializer::Write(*enemy);

    CHECK(saved.Find("type")->AsString() == "SaveTestEnemy");
    const ArchiveValue& properties = *saved.Find("properties");
    CHECK(properties.GetMemberName(0) == "name"); // Actor のプロパティが先
    CHECK(properties.Find("name")->AsString() == "スライム");
    CHECK(properties.Find("x")->AsFloat() == doctest::Approx(12.5));
    CHECK(properties.Find("hp")->AsInt() == -3);
    CHECK(properties.Find("angry")->AsBool());
    CHECK(properties.Find("target") == nullptr);     // ハンドルは書かない
    CHECK(properties.Find("followers") == nullptr);
    CHECK(properties.Find("components") == nullptr); // Actor のハンドルの配列も書かない
}

TEST_CASE("ObjectSerializer: JSON の文字列を通して、別のオブジェクトに同じ値を読み込める")
{
    ObjectRegistry registry;
    SaveTestEnemy* original = registry.Create<SaveTestEnemy>();
    original->name = "ゴブリン";
    original->x = 0.1f;
    original->y = -7.25f;
    original->angry = true;
    original->hp = 2147483647; // int32 の最大
    original->speed = 3.3f;
    original->title = "隊長 \"A\"";

    std::string text;
    REQUIRE(JsonFormat::Write(ObjectSerializer::Write(*original), text));
    ArchiveValue loaded;
    REQUIRE(JsonFormat::Read(text, loaded));

    SaveTestEnemy* copy = registry.Create<SaveTestEnemy>();
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(loaded, *copy);

    CHECK(report.warnings.empty());
    CHECK(report.readCount == 7); // name, x, y, angry, hp, speed, title
    CHECK(copy->name == "ゴブリン");
    CHECK(copy->x == 0.1f); // float は文字列を通しても、ぴったり同じ値に戻る
    CHECK(copy->y == -7.25f);
    CHECK(copy->angry);
    CHECK(copy->hp == 2147483647);
    CHECK(copy->speed == 3.3f);
    CHECK(copy->title == "隊長 \"A\"");
}

TEST_CASE("ObjectSerializer: float は短い形で書かれる(0.1f は 0.1)")
{
    ObjectRegistry registry;
    SaveTestEnemy* enemy = registry.Create<SaveTestEnemy>();
    enemy->x = 0.1f;

    std::string text;
    REQUIRE(JsonFormat::Write(ObjectSerializer::Write(*enemy), text, false));
    CHECK(text.find("\"x\":0.1,") != std::string::npos);
}

TEST_CASE("ObjectSerializer: 無いプロパティは今の値のまま。知らない名前や型の違いは飛ばして、理由を残す")
{
    ObjectRegistry registry;
    SaveTestEnemy* enemy = registry.Create<SaveTestEnemy>();

    ArchiveValue loaded;
    REQUIRE(JsonFormat::Read(R"({
        "type": "SomethingElse",
        "properties": {
            "hp": "たくさん",
            "speed": 2,
            "unknown": 1,
            "angry": true,
            "target": 5,
            "x": 3000000000
        }
    })", loaded));

    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(loaded, *enemy);

    CHECK(enemy->hp == 10);                       // 型が違う → そのまま
    CHECK(enemy->speed == 2.0f);                  // 整数でも float に読める
    CHECK(enemy->angry);
    CHECK(enemy->title == "なし");                // ファイルに無い → そのまま
    CHECK(enemy->x == doctest::Approx(3.0e9f));   // x は float なので、大きな整数も読める
    CHECK(report.readCount == 3);                 // speed, angry, x
    REQUIRE(report.warnings.size() == 4);         // 型の名前、hp、unknown、target
    CHECK(report.warnings[0].find("型が違います") != std::string::npos);
}

TEST_CASE("ObjectSerializer: int32 の範囲を超える整数は読み込まない")
{
    ObjectRegistry registry;
    SaveTestEnemy* enemy = registry.Create<SaveTestEnemy>();
    ArchiveValue value = ArchiveValue::MakeObject();
    value.Set("properties", ArchiveValue::MakeObject()).Set("hp", ArchiveValue(std::int64_t{3000000000}));

    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(value, *enemy);

    CHECK(enemy->hp == 10);
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("int32") != std::string::npos);
}
