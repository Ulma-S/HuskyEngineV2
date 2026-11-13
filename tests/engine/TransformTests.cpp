// Transform(アクターの位置・回転・拡大縮小)のテスト。保存の形と、読み戻しを確かめる。
#include "engine/Transform.h"

#include "core/ObjectRegistry.h"
#include "engine/Actor.h"
#include "engine/GlmConversion.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp> // glm::translate

#include <string>

using husky::engine::Actor;
using husky::engine::ArchiveValue;
namespace ObjectSerializer = husky::engine::ObjectSerializer;
namespace JsonFormat = husky::engine::JsonFormat;

TEST_CASE("Transform: 既定は原点、回転 0、拡大縮小 1")
{
    const husky::engine::Transform transform;
    CHECK(transform.position == husky::core::Vector3{});
    CHECK(transform.rotation == 0.0f);
    CHECK(transform.scale == (husky::core::Vector3{1.0f, 1.0f, 1.0f}));
}

TEST_CASE("Transform: 保存すると入れ子のオブジェクトになり、別のアクターにそのまま読み戻せる")
{
    husky::core::ObjectRegistry registry;
    Actor* original = registry.Create<Actor>();
    REQUIRE(original != nullptr);
    original->transform.position = husky::core::Vector3{1.5f, -2.0f, 3.0f};
    original->transform.rotation = 90.0f;
    original->transform.scale = husky::core::Vector3{2.0f, 0.5f, 1.0f};

    const ArchiveValue saved = ObjectSerializer::Write(*original);
    const ArchiveValue* transform = saved.Find("properties")->Find("transform");
    REQUIRE(transform != nullptr);
    // {"position": {"x": 1.5, "y": -2.0, "z": 3.0}, "rotation": 90.0, "scale": {"x": 2.0, "y": 0.5, "z": 1.0}}
    CHECK(transform->Find("position")->Find("y")->AsFloat() == doctest::Approx(-2.0));
    CHECK(transform->Find("rotation")->AsFloat() == doctest::Approx(90.0));
    CHECK(transform->Find("scale")->Find("x")->AsFloat() == doctest::Approx(2.0));

    // JSON の文字列を通して読み戻す
    std::string text;
    JsonFormat::Write(saved, text, false);
    ArchiveValue loaded;
    REQUIRE(JsonFormat::Read(text.c_str(), loaded));
    Actor* copy = registry.Create<Actor>();
    REQUIRE(copy != nullptr);
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(loaded, *copy);
    CHECK(report.warnings.empty());
    CHECK(copy->transform.position == original->transform.position);
    CHECK(copy->transform.rotation == original->transform.rotation);
    CHECK(copy->transform.scale == original->transform.scale);
}

TEST_CASE("Transform: 古い形(位置が x と y だけ)のファイルは、知らないプロパティとして警告が出る")
{
    husky::core::ObjectRegistry registry;
    Actor* actor = registry.Create<Actor>();
    REQUIRE(actor != nullptr);
    ArchiveValue loaded;
    REQUIRE(JsonFormat::Read(R"({"type": "Actor", "properties": {"x": 10, "y": 20}})", loaded));
    const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(loaded, *actor);
    CHECK(report.warnings.size() == 2); // x と y はもう無い(黙って読み捨てない)
    CHECK(actor->transform.position == husky::core::Vector3{});
}

TEST_CASE("Vector3: 計算するときは glm に変え、結果を自前の型に戻せる(値はそのまま)")
{
    const husky::core::Vector3 position{1.0f, 2.0f, 3.0f};
    const glm::vec3 moved = husky::engine::ToGlm(position) + glm::vec3(10.0f, 0.0f, 0.0f); // 計算は glm で
    CHECK(husky::engine::FromGlm(moved) == (husky::core::Vector3{11.0f, 2.0f, 3.0f}));
}

TEST_CASE("Matrix4: 既定は単位行列で、glm の行列と同じ並び(列優先)のまま行き来できる")
{
    const husky::core::Matrix4 identity;
    CHECK(husky::engine::FromGlm(glm::mat4(1.0f)) == identity);

    // 移動の行列: x の移動量は 4 列目の 1 行目 = m[12]
    const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 6.0f, 7.0f));
    const husky::core::Matrix4 converted = husky::engine::FromGlm(moved);
    CHECK(converted.m[12] == 5.0f);
    CHECK(converted.m[13] == 6.0f);
    CHECK(converted.m[14] == 7.0f);
    CHECK(husky::engine::ToGlm(converted) == moved); // 行って戻っても同じ
}
