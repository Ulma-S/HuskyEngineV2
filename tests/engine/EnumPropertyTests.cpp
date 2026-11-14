// enum のプロパティ(core/EnumReflection.h。SF-3b)のテスト。保存は値の名前、読むときは名前(か登録した数)。
#include "core/EnumReflection.h"

#include "core/Object.h"
#include "core/ObjectRegistry.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::ObjectRegistry;
using husky::core::PropertyType;
using husky::engine::ArchiveValue;
namespace ObjectSerializer = husky::engine::ObjectSerializer;

namespace
{
    // 数を飛ばした enum class(並びを変えても、名前で保存するのでずれない)
    enum class Team
    {
        Red = 1,
        Blue = 5,
        Green,
    };
    HUSKY_ENUM_BEGIN(Team)
        HUSKY_ENUM_VALUE(Red)
        HUSKY_ENUM_VALUE(Blue)
        HUSKY_ENUM_VALUE(Green)
    HUSKY_ENUM_END()

    // 大きさの違う enum(1 バイト)。get / set が大きさに合わせて読み書きする
    enum class Size : std::uint8_t
    {
        Small,
        Large = 200,
    };
    HUSKY_ENUM_BEGIN(Size)
        HUSKY_ENUM_VALUE(Small)
        HUSKY_ENUM_VALUE(Large)
    HUSKY_ENUM_END()

    class Unit : public husky::core::Object
    {
        HUSKY_OBJECT_INLINE(Unit, husky::core::Object)
    public:
        Team team = Team::Red;
        Size size = Size::Small;
        std::vector<Team> allies;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(team)
            HUSKY_PROPERTY(size)
            HUSKY_PROPERTY(allies)
        HUSKY_REFLECTION_END()
    };

    std::string ToJson(const ArchiveValue& value)
    {
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(value, text));
        return text;
    }

    ArchiveValue FromJson(const std::string& text)
    {
        ArchiveValue value;
        REQUIRE(husky::engine::JsonFormat::Read(text, value));
        return value;
    }
}

TEST_CASE("SF-3b: 登録した enum はプロパティになり、名前の表(EnumInfo)を持つ")
{
    const husky::core::Property* team = Unit::StaticType().FindProperty("team");
    REQUIRE(team != nullptr);
    CHECK(team->GetType() == PropertyType::Enum);
    REQUIRE(team->GetEnumInfo() != nullptr);
    CHECK(std::string(team->GetEnumInfo()->name) == "Team");
    REQUIRE(team->GetEnumInfo()->entries.size() == 3);
    CHECK(std::string(team->GetEnumInfo()->FindName(5)) == "Blue");
    CHECK(team->GetEnumInfo()->FindName(2) == nullptr);
    CHECK(Unit::StaticType().FindProperty("allies")->GetArrayInfo()->element.GetEnumInfo() == team->GetEnumInfo());
}

TEST_CASE("SF-3b: enum は値の名前で保存され、読み直すと同じ値になる(1 バイトの enum と、enum の配列も)")
{
    ObjectRegistry registry;
    Unit* source = registry.Create<Unit>();
    REQUIRE(source != nullptr);
    source->team = Team::Green;
    source->size = Size::Large;
    source->allies = {Team::Blue, Team::Red};

    const std::string text = ToJson(ObjectSerializer::Write(*source));
    CHECK(text.find("\"Green\"") != std::string::npos);
    CHECK(text.find("\"Large\"") != std::string::npos);
    CHECK(text.find("\"Blue\"") != std::string::npos);

    Unit* loaded = registry.Create<Unit>();
    REQUIRE(loaded != nullptr);
    CHECK(ObjectSerializer::ReadProperties(FromJson(text), *loaded).warnings.empty());
    CHECK(loaded->team == Team::Green);
    CHECK(loaded->size == Size::Large);
    CHECK(loaded->allies == std::vector<Team>{Team::Blue, Team::Red});
}

TEST_CASE("SF-3b: 数でも登録した値なら読む。知らない名前・知らない数は読まずに警告。登録していない値は数で保存する")
{
    ObjectRegistry registry;
    Unit* unit = registry.Create<Unit>();
    REQUIRE(unit != nullptr);
    const ObjectSerializer::ReadReport report =
        ObjectSerializer::ReadProperties(FromJson(R"({ "properties": { "team": 5, "size": "Huge", "allies": [3] } })"), *unit);
    CHECK(unit->team == Team::Blue); // 5 は Blue
    CHECK(unit->size == Size::Small);  // 知らない名前は読まない
    REQUIRE(report.warnings.size() == 2);
    CHECK(report.warnings[0].find("Huge") != std::string::npos);
    CHECK(report.warnings[1].find("[0]") != std::string::npos); // 3 は登録していない

    unit->team = static_cast<Team>(42); // 登録していない値(コードでは作れてしまう)
    const ArchiveValue saved = ObjectSerializer::Write(*unit);
    const ArchiveValue* team = saved.Find("properties")->Find("team");
    REQUIRE(team != nullptr);
    CHECK(team->GetType() == ArchiveValue::Type::Int);
    CHECK(team->AsInt() == 42);
}
