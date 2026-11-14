// ObjectSerializer::Matches(値の木を作らずに、前に書いた値と同じかを比べる)のテスト。
// 「Write して Equals」と同じ答えになることと、比べるときにメモリを確保しないことを確かめる。
#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/MemoryTracker.h"
#include "core/Object.h"
#include "core/ObjectRegistry.h"
#include "core/Vector2.h"
#include "engine/ObjectSerializer.h"
#include "engine/TextureAsset.h"
#include "engine/UIComponents.h" // TextAlign(enum の登録)

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    // プロパティにできる型を一通り持つ(ハンドルは対応表なしでは書かないので、比べない)
    class MatchSample : public husky::core::Object
    {
        HUSKY_OBJECT(MatchSample, husky::core::Object)
    public:
        bool flag = true;
        std::int32_t count = 3;
        float speed = 1.5f;
        std::int64_t big = 1234567890123LL;
        double precise = 0.1;
        husky::engine::TextAlign align = husky::engine::TextAlign::Center;
        std::string label = "こんにちは";
        husky::core::Vector2 point = {1.0f, 2.0f};
        std::vector<float> values = {0.5f, 0.25f};
        std::vector<bool> switches = {true, false};
        std::vector<std::string> names = {"a", "b"};
        husky::core::AssetRef<husky::engine::TextureAsset> texture;
        husky::core::Guid id;
        husky::core::ObjectHandle target; // 比べない

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(flag)
            HUSKY_PROPERTY(count)
            HUSKY_PROPERTY(speed)
            HUSKY_PROPERTY(big)
            HUSKY_PROPERTY(precise)
            HUSKY_PROPERTY(align)
            HUSKY_PROPERTY(label)
            HUSKY_PROPERTY(point)
            HUSKY_PROPERTY(values)
            HUSKY_PROPERTY(switches)
            HUSKY_PROPERTY(names)
            HUSKY_PROPERTY(texture)
            HUSKY_PROPERTY(id)
            HUSKY_PROPERTY(target)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("ObjectSerializer::Matches: 書いた直後は同じ。どの型の値が変わっても違いが分かり、Write して Equals と同じ答え")
{
    husky::core::ObjectRegistry registry;
    MatchSample* sample = registry.Create<MatchSample>();
    REQUIRE(sample != nullptr);
    REQUIRE(husky::core::Guid::Parse("0a2f250f-023f-45b3-b5fd-192a91cd42f9", sample->id));
    sample->texture.guid = sample->id;
    const husky::engine::ArchiveValue written = husky::engine::ObjectSerializer::Write(*sample);
    CHECK(husky::engine::ObjectSerializer::Matches(*sample, written));

    // 1つずつ変えて「違う」になり、戻すと「同じ」に戻る。答えは Write + Equals と同じ
    auto check = [&](auto&& change, auto&& restore) {
        change();
        const bool matches = husky::engine::ObjectSerializer::Matches(*sample, written);
        CHECK_FALSE(matches);
        CHECK(matches == husky::engine::ObjectSerializer::Write(*sample).Equals(written));
        restore();
        CHECK(husky::engine::ObjectSerializer::Matches(*sample, written));
    };
    check([&] { sample->flag = false; }, [&] { sample->flag = true; });
    check([&] { sample->count = 4; }, [&] { sample->count = 3; });
    check([&] { sample->speed = 1.75f; }, [&] { sample->speed = 1.5f; });
    check([&] { sample->big += 1; }, [&] { sample->big -= 1; });
    check([&] { sample->precise = 0.2; }, [&] { sample->precise = 0.1; });
    check([&] { sample->align = husky::engine::TextAlign::Right; }, [&] { sample->align = husky::engine::TextAlign::Center; });
    check([&] { sample->label = "さようなら"; }, [&] { sample->label = "こんにちは"; });
    check([&] { sample->point.y = 3.0f; }, [&] { sample->point.y = 2.0f; });
    check([&] { sample->values[1] = 0.75f; }, [&] { sample->values[1] = 0.25f; });
    check([&] { sample->values.push_back(1.0f); }, [&] { sample->values.pop_back(); });
    check([&] { sample->switches[1] = true; }, [&] { sample->switches[1] = false; });
    check([&] { sample->names[0] = "z"; }, [&] { sample->names[0] = "a"; });
    check([&] { sample->texture.guid = husky::core::Guid{}; }, [&] { sample->texture.guid = sample->id; });
    check([&] { sample->id.low ^= 1; }, [&] { sample->id.low ^= 1; });

    // ハンドルは比べない(対応表なしの Write は書かないので)
    sample->target = sample->GetHandle();
    CHECK(husky::engine::ObjectSerializer::Matches(*sample, written));
}

TEST_CASE("ObjectSerializer::Matches: 比べるときにメモリを確保しない")
{
    husky::core::ObjectRegistry registry;
    MatchSample* sample = registry.Create<MatchSample>();
    REQUIRE(sample != nullptr);
    const husky::engine::ArchiveValue written = husky::engine::ObjectSerializer::Write(*sample);
    const std::uint64_t before = husky::core::GetMemoryStats().totalAllocations;
    bool all = true;
    for (int i = 0; i < 100; ++i)
    {
        all = all && husky::engine::ObjectSerializer::Matches(*sample, written);
    }
    CHECK(husky::core::GetMemoryStats().totalAllocations == before);
    CHECK(all);
}
