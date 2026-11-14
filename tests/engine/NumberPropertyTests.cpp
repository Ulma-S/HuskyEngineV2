// std::int64_t と double のプロパティ(SF-3a)のテスト。保存して読み直すと、細かい値まで同じになる。
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
    class Score : public husky::core::Object
    {
        HUSKY_OBJECT_INLINE(Score, husky::core::Object)
    public:
        std::int64_t total = 0;
        double ratio = 0.0;
        std::vector<std::int64_t> history;
        std::vector<double> times;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(total)
            HUSKY_PROPERTY(ratio)
            HUSKY_PROPERTY(history)
            HUSKY_PROPERTY(times)
        HUSKY_REFLECTION_END()
    };

    ArchiveValue ThroughJson(const ArchiveValue& value)
    {
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(value, text));
        ArchiveValue loaded;
        REQUIRE(husky::engine::JsonFormat::Read(text, loaded));
        return loaded;
    }
}

TEST_CASE("SF-3a: int64 と double はプロパティになり、保存して読み直すと細かい値まで同じになる(配列も)")
{
    CHECK(Score::StaticType().FindProperty("total")->GetType() == PropertyType::Int64);
    CHECK(Score::StaticType().FindProperty("ratio")->GetType() == PropertyType::Double);
    CHECK(Score::StaticType().FindProperty("times")->GetArrayInfo()->element.GetType() == PropertyType::Double);

    ObjectRegistry registry;
    Score* source = registry.Create<Score>();
    REQUIRE(source != nullptr);
    source->total = 9007199254740993; // 2^53 + 1: double では表せない整数(int64 なら表せる)
    source->ratio = 0.1 + 0.2;        // 0.30000000000000004(float に丸めると違う値になる)
    source->history = {-5000000000, 7};
    source->times = {1.0 / 3.0};

    Score* loaded = registry.Create<Score>();
    REQUIRE(loaded != nullptr);
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(ThroughJson(ObjectSerializer::Write(*source)), *loaded);
    CHECK(report.warnings.empty());
    CHECK(loaded->total == 9007199254740993);
    CHECK(loaded->ratio == 0.1 + 0.2);
    CHECK(loaded->history == std::vector<std::int64_t>{-5000000000, 7});
    REQUIRE(loaded->times.size() == 1);
    CHECK(loaded->times[0] == 1.0 / 3.0);
}

TEST_CASE("SF-3a: int64 は小数を断り、double は整数でも読める")
{
    ObjectRegistry registry;
    Score* score = registry.Create<Score>();
    REQUIRE(score != nullptr);
    ArchiveValue value;
    REQUIRE(husky::engine::JsonFormat::Read(R"({ "properties": { "total": 1.5, "ratio": 2 } })", value));
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(value, *score);
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("total") != std::string::npos);
    CHECK(score->total == 0);
    CHECK(score->ratio == 2.0);
}
