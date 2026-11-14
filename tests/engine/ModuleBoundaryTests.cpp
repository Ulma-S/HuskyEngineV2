// HR-1: エンジンを DLL(HuskyEngine.dll)にしても、型情報などの「プログラムに1つ」のものが1つのままか。
// このテストの exe はエンジンとは別のモジュール。ここで取った型情報のアドレスと、DLL の中のエンジンが
// 登録した型情報のアドレスが同じなら、写しは1つだけ(静的リンクの構成でも、もちろん同じになる)。
#include "core/TypeRegistry.h"
#include "engine/Actor.h"
#include "engine/EngineTypes.h"
#include "engine/Rigidbody2D.h"
#include "engine/SpriteComponent.h"
#include "engine/Transform.h"

#include <doctest/doctest.h>

#include <vector>

namespace
{
    // テストの exe の中だけの型(HUSKY_OBJECT_INLINE)。親はエンジンの型。
    class BoundaryTestComponent : public husky::engine::Component
    {
        HUSKY_OBJECT_INLINE(BoundaryTestComponent, husky::engine::Component)
    public:
        std::vector<float> values;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(values)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("HR-1: エンジンの型情報は、DLL の外から見ても DLL の中で登録したものと同じ1つ")
{
    husky::core::TypeRegistry types;
    husky::engine::RegisterEngineTypes(types); // 登録は DLL の中で行われる

    CHECK(types.Find("Actor") == &husky::engine::Actor::StaticType());
    CHECK(types.Find("SpriteComponent") == &husky::engine::SpriteComponent::StaticType());

    // 構造体と enum の型情報も同じ1つ(DLL の中で作ったプロパティの型と、ここで引いた型が同じアドレス)
    const husky::core::Property* transform = husky::engine::Actor::StaticType().FindProperty("transform");
    REQUIRE(transform != nullptr);
    CHECK(transform->GetStructType() == husky::core::StructTypeOf<husky::engine::Transform>());
    const husky::core::Property* bodyType = husky::engine::Rigidbody2D::StaticType().FindProperty("bodyType");
    REQUIRE(bodyType != nullptr);
    CHECK(bodyType->GetEnumInfo() == husky::core::EnumInfoOf<husky::engine::BodyType2D>());
}

TEST_CASE("HR-1: DLL の外の型も、エンジンの型の子として見分けられる")
{
    BoundaryTestComponent component;
    // IsA は親をたどってアドレスで比べる。親の Component の型情報が DLL の中の1つなので true になる。
    CHECK(component.IsA<husky::engine::Component>());
    CHECK(component.GetType().IsA(husky::engine::Component::StaticType()));
    CHECK_FALSE(component.IsA<husky::engine::Actor>());

    // 配列のプロパティを型を指定して読める(ArrayInfoOf はモジュールごとに写しができるが、要素の型で比べる)
    const husky::core::Property* values = component.GetType().FindProperty("values");
    REQUIRE(values != nullptr);
    CHECK(values->GetValuePtr<std::vector<float>>(component) != nullptr);
}
