// 型の付いたオブジェクトの参照 ObjectRef<T>(core/ObjectRef.h。SF-1)のテスト。
#include "core/ObjectRef.h"

#include "core/ObjectRegistry.h"
#include "core/TypeRegistry.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteComponent.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::ObjectRef;
using husky::core::ObjectRegistry;
using husky::core::TypeRegistry;
using husky::engine::Actor;
using husky::engine::ArchiveValue;
using husky::engine::Scene;
using husky::engine::SpriteComponent;
namespace SceneSerializer = husky::engine::SceneSerializer;

namespace
{
    // 狙われる側(型の印に使う)。
    class Prey : public Actor
    {
        HUSKY_OBJECT_INLINE(Prey, Actor)
    };

    // アクターとコンポーネントを、型の付いた参照で指す(private のメンバーでも、クラスの中で登録すれば保存される)。
    class Hunter : public Actor
    {
        HUSKY_OBJECT_INLINE(Hunter, Actor)
        ObjectRef<Prey> m_prey;
        ObjectRef<SpriteComponent> m_sprite;

    public:
        ObjectRef<Prey>& Target() { return m_prey; }
        ObjectRef<SpriteComponent>& Sprite() { return m_sprite; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(m_prey)
            HUSKY_PROPERTY(m_sprite)
        HUSKY_REFLECTION_END()
    };

    Scene* CreateRootScene(ObjectRegistry& registry)
    {
        Scene* scene = registry.Create<Scene>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    TypeRegistry MakeTypes()
    {
        TypeRegistry types;
        husky::engine::RegisterEngineTypes(types);
        types.Register<Prey>();
        types.Register<Hunter>();
        return types;
    }

    ArchiveValue ThroughJson(const ArchiveValue& value)
    {
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(value, text));
        ArchiveValue loaded;
        REQUIRE(husky::engine::JsonFormat::Read(text, loaded));
        return loaded;
    }
}

TEST_CASE("SF-1: ObjectRef<T> はプロパティになり、指せる型の情報を持つ。Resolve は型の違う相手や消えた相手には nullptr")
{
    const husky::core::Property* property = Hunter::StaticType().FindProperty("m_prey");
    REQUIRE(property != nullptr);
    CHECK(property->GetType() == husky::core::PropertyType::ObjectRef);
    CHECK(property->GetObjectRefType() == &Prey::StaticType());
    CHECK(property->IsReference()); // シーンのファイルでは番号で書く

    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Hunter* hunter = scene->Spawn<Hunter>();
    Prey* prey = scene->Spawn<Prey>();
    CHECK(hunter->Target().Resolve(registry) == nullptr); // まだ何も指さない
    hunter->Target().Set(prey);
    CHECK(hunter->Target().Resolve(registry) == prey);

    // 中身は ObjectHandle なので、型の違う相手を入れることはできる。そのときも Resolve は nullptr(Cast が失敗する)
    hunter->Target().handle = hunter->GetHandle();
    CHECK(hunter->Target().Resolve(registry) == nullptr);

    // 指している間は GC に回収されない(強い参照)。消えたら nullptr
    hunter->Target().Set(prey);
    scene->actors.pop_back(); // シーンの一覧から外す(残るのは hunter からの参照だけ)
    registry.CollectGarbage();
    CHECK(hunter->Target().Resolve(registry) == prey);
    prey->Destroy();
    registry.FlushDestroyed();
    CHECK(hunter->Target().Resolve(registry) == nullptr);
}

TEST_CASE("SF-1: シーンを保存して読み直すと、ObjectRef は読み直した相手(アクターとコンポーネント)につながる")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry sourceRegistry;
    Scene* source = CreateRootScene(sourceRegistry);
    Prey* prey = source->Spawn<Prey>();
    prey->name = "うさぎ";
    SpriteComponent* sprite = prey->AddComponent<SpriteComponent>();
    sprite->width = 12.0f;
    Hunter* hunter = source->Spawn<Hunter>();
    hunter->Target().Set(prey);
    hunter->Sprite().Set(sprite);

    const SceneSerializer::WriteResult saved = SceneSerializer::Write(*source);
    CHECK(saved.warnings.empty());

    ObjectRegistry targetRegistry;
    targetRegistry.Create<Actor>(); // ハンドルの場所をずらす
    Scene* target = CreateRootScene(targetRegistry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(ThroughJson(saved.value), *target, types);
    CHECK(report.warnings.empty());
    REQUIRE(target->actors.size() == 2);
    Prey* loadedPrey = targetRegistry.Resolve<Prey>(target->actors[0]);
    Hunter* loadedHunter = targetRegistry.Resolve<Hunter>(target->actors[1]);
    REQUIRE(loadedPrey != nullptr);
    REQUIRE(loadedHunter != nullptr);
    CHECK(loadedHunter->Target().Resolve(targetRegistry) == loadedPrey);
    const SpriteComponent* loadedSprite = loadedHunter->Sprite().Resolve(targetRegistry);
    REQUIRE(loadedSprite != nullptr);
    CHECK(loadedSprite == loadedPrey->GetComponent<SpriteComponent>());
    CHECK(loadedSprite->width == 12.0f);
}

TEST_CASE("SF-1: 読み込みで、番号の相手が指せる型でなければ、つながずに null にして警告する")
{
    const TypeRegistry types = MakeTypes();
    // m_prey が、Prey ではないアクター(番号 0 の Actor)を指しているファイル
    ArchiveValue file;
    REQUIRE(husky::engine::JsonFormat::Read(R"({ "version": 1, "objects": [
        { "id": 0, "type": "Actor", "properties": { "name": "NotPrey" } },
        { "id": 1, "type": "Hunter", "properties": { "name": "Hunter", "m_prey": 0 } }
    ], "actors": [0, 1] })", file));

    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("m_prey") != std::string::npos);
    CHECK(report.warnings[0].find("Prey") != std::string::npos);
    REQUIRE(scene->actors.size() == 2);
    Hunter* hunter = registry.Resolve<Hunter>(scene->actors[1]);
    REQUIRE(hunter != nullptr);
    CHECK(hunter->Target().IsNull());
}

TEST_CASE("SF-1: プレハブにすると、アクターの外を指す ObjectRef は null になり、中のコンポーネントを指すものは残る")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Prey* outside = scene->Spawn<Prey>();
    Hunter* hunter = scene->Spawn<Hunter>();
    SpriteComponent* own = hunter->AddComponent<SpriteComponent>();
    hunter->Target().Set(outside);
    hunter->Sprite().Set(own);

    const husky::engine::Prefab::WriteResult saved = husky::engine::Prefab::Write(*hunter);
    REQUIRE(saved.ok);
    CHECK_FALSE(saved.warnings.empty()); // 外を指す m_prey を null にした
    const husky::engine::Prefab::InstantiateResult placed = husky::engine::Prefab::Instantiate(saved.value, *scene, types);
    Hunter* copy = husky::core::Cast<Hunter>(placed.actor);
    REQUIRE(copy != nullptr);
    CHECK(copy->Target().IsNull());
    CHECK(copy->Sprite().Resolve(registry) == copy->GetComponent<SpriteComponent>());
}
