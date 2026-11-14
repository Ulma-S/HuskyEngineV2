// 配列のプロパティ std::vector<E>(SF-2a)のテスト。保存と読み込み、GC、プレハブとの違い。
#include "core/AssetRef.h"
#include "core/ObjectRef.h"
#include "core/ObjectRegistry.h"
#include "core/StructReflection.h"
#include "core/TypeRegistry.h"
#include "engine/Component.h"
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Prefab.h"
#include "engine/PrefabAsset.h"
#include "engine/World.h"
#include "engine/SceneSerializer.h"
#include "engine/Application.h"
#include "engine/AssetDatabase.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::ObjectRef;
using husky::core::ObjectRegistry;
using husky::core::PropertyType;
using husky::core::TypeRegistry;
using husky::engine::Actor;
using husky::engine::ArchiveValue;
using husky::engine::World;
namespace SceneSerializer = husky::engine::SceneSerializer;

namespace
{
    class Waypoint : public Actor
    {
        HUSKY_OBJECT_INLINE(Waypoint, Actor)
    };

    // 構造体の要素(中に型の付いた参照も持つ)。
    struct Stop
    {
        float wait = 1.0f;
        ObjectRef<Waypoint> at;
    };
    HUSKY_STRUCT_BEGIN(Stop)
        HUSKY_PROPERTY(wait)
        HUSKY_PROPERTY(at)
    HUSKY_STRUCT_END()

    // いろいろな配列を持つコンポーネント(private でもよい)。
    class Route : public husky::engine::Component
    {
        HUSKY_OBJECT_INLINE(Route, husky::engine::Component)
    public:
        std::vector<std::int32_t> ids;
        std::vector<float> speeds;
        std::vector<std::string> names;
        std::vector<bool> flags; // 要素の場所が取れない配列(写して触って戻す)
        std::vector<Stop> stops;
        std::vector<ObjectRef<Waypoint>> points;
        std::vector<husky::core::AssetRef<husky::engine::PrefabAsset>> prefabs;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(ids)
            HUSKY_PROPERTY(speeds)
            HUSKY_PROPERTY(names)
            HUSKY_PROPERTY(flags)
            HUSKY_PROPERTY(stops)
            HUSKY_PROPERTY(points)
            HUSKY_PROPERTY(prefabs)
        HUSKY_REFLECTION_END()
    };

    World* CreateRootScene(ObjectRegistry& registry)
    {
        World* scene = registry.Create<World>();
        registry.AddRoot(scene->GetHandle());
        return scene;
    }

    TypeRegistry MakeTypes()
    {
        TypeRegistry types;
        husky::engine::RegisterEngineTypes(types);
        types.Register<Waypoint>();
        types.Register<Route>();
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

    const husky::core::Guid PrefabGuid = husky::tests::MakeGuid(0x8e1a2b3c4d5e4f60ull, 0x9a9b0c1d2e3f4a02ull);
}

TEST_CASE("SF-2a: std::vector<E> はプロパティになり、要素の型と扱い方(ArrayInfo)を持つ")
{
    const husky::core::Property* ids = Route::StaticType().FindProperty("ids");
    REQUIRE(ids != nullptr);
    CHECK(ids->GetType() == PropertyType::Array);
    REQUIRE(ids->GetArrayInfo() != nullptr);
    CHECK(ids->GetArrayInfo()->element.GetType() == PropertyType::Int32);
    CHECK_FALSE(ids->IsReference());

    const husky::core::Property* stops = Route::StaticType().FindProperty("stops");
    REQUIRE(stops != nullptr);
    CHECK(stops->GetArrayInfo()->element.GetType() == PropertyType::Struct);
    CHECK_FALSE(stops->IsReference()); // 構造体の要素の中の参照は、要素のメンバーの側で数える(構造体のプロパティと同じ)

    const husky::core::Property* points = Route::StaticType().FindProperty("points");
    REQUIRE(points != nullptr);
    CHECK(points->GetArrayInfo()->element.GetObjectRefType() == &Waypoint::StaticType());
    CHECK(points->IsReference()); // 参照の配列は、シーンのファイルで番号の対応表が要る

    // 同じ要素の型の配列は、同じ ArrayInfo を使う(要素の型ごとに1つ)
    CHECK(husky::core::ArrayInfoOf<std::vector<std::int32_t>>() == ids->GetArrayInfo());
}

TEST_CASE("SF-2a: 配列を保存して読み直すと、数・文字列・構造体・参照・アセットの参照の配列が同じになる")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry sourceRegistry;
    World* source = CreateRootScene(sourceRegistry);
    Waypoint* a = source->Spawn<Waypoint>();
    Waypoint* b = source->Spawn<Waypoint>();
    Actor* holder = source->Spawn<Actor>();
    Route* route = holder->AddComponent<Route>();
    route->ids = {3, 1, 4};
    route->speeds = {0.5f, 2.0f};
    route->names = {"start", "goal"};
    route->flags = {true, false, true};
    route->stops.resize(2);
    route->stops[0].wait = 0.25f;
    route->stops[0].at.Set(b);
    route->stops[1].at.Set(a);
    route->points.resize(3);
    route->points[0].Set(a);
    route->points[2].Set(b); // 真ん中は何も指さない(並びの位置は変えずに null で保存する)
    route->prefabs.resize(1);
    route->prefabs[0].guid = PrefabGuid;

    const SceneSerializer::WriteResult saved = SceneSerializer::Write(*source);
    CHECK(saved.warnings.empty());

    ObjectRegistry targetRegistry;
    targetRegistry.Create<Actor>(); // ハンドルの場所をずらす
    World* target = CreateRootScene(targetRegistry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(ThroughJson(saved.value), *target, types);
    CHECK(report.warnings.empty());
    REQUIRE(target->actors.size() == 3);
    Waypoint* loadedA = targetRegistry.Resolve<Waypoint>(target->actors[0]);
    Waypoint* loadedB = targetRegistry.Resolve<Waypoint>(target->actors[1]);
    Actor* loadedHolder = targetRegistry.Resolve<Actor>(target->actors[2]);
    REQUIRE(loadedHolder != nullptr);
    const Route* loaded = loadedHolder->GetComponent<Route>();
    REQUIRE(loaded != nullptr);
    CHECK(loaded->ids == std::vector<std::int32_t>{3, 1, 4});
    CHECK(loaded->speeds == std::vector<float>{0.5f, 2.0f});
    CHECK(loaded->names == std::vector<std::string>{"start", "goal"});
    CHECK(loaded->flags == std::vector<bool>{true, false, true});
    REQUIRE(loaded->stops.size() == 2);
    CHECK(loaded->stops[0].wait == 0.25f);
    CHECK(loaded->stops[0].at.Resolve(targetRegistry) == loadedB);
    CHECK(loaded->stops[1].wait == 1.0f);
    CHECK(loaded->stops[1].at.Resolve(targetRegistry) == loadedA);
    REQUIRE(loaded->points.size() == 3);
    CHECK(loaded->points[0].Resolve(targetRegistry) == loadedA);
    CHECK(loaded->points[1].IsNull());
    CHECK(loaded->points[2].Resolve(targetRegistry) == loadedB);
    REQUIRE(loaded->prefabs.size() == 1);
    CHECK(loaded->prefabs[0].guid == PrefabGuid);
}

TEST_CASE("SF-2a: 配列の中の参照も GC がたどる(強い参照)")
{
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    Waypoint* direct = scene->Spawn<Waypoint>();
    Waypoint* inStruct = scene->Spawn<Waypoint>();
    Actor* holder = scene->Spawn<Actor>();
    Route* route = holder->AddComponent<Route>();
    route->points.resize(1);
    route->points[0].Set(direct);
    route->stops.resize(1);
    route->stops[0].at.Set(inStruct);
    scene->actors.erase(scene->actors.begin(), scene->actors.begin() + 2); // 2つの道しるべを一覧から外す

    registry.CollectGarbage();
    CHECK(route->points[0].Resolve(registry) == direct);
    CHECK(route->stops[0].at.Resolve(registry) == inStruct);

    route->points.clear();
    route->stops.clear();
    CHECK(registry.CollectGarbage() >= 2); // 指すものが無くなったので回収される
}

TEST_CASE("SF-2a: 読めない要素があっても、ほかの要素は読み、何番目かの付いた警告を出す。配列でなければ読まない")
{
    const TypeRegistry types = MakeTypes();
    ArchiveValue file;
    REQUIRE(husky::engine::JsonFormat::Read(R"({ "version": 1, "objects": [
        { "id": 0, "type": "Actor", "properties": { "name": "Holder", "components": [1] } },
        { "id": 1, "type": "Route", "properties": { "ids": [7, "x", 9], "speeds": 3 } }
    ], "actors": [0] })", file));
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);
    REQUIRE(report.warnings.size() == 2);
    CHECK(report.warnings[0].find("[1]") != std::string::npos);
    CHECK(report.warnings[1].find("配列ではありません") != std::string::npos);
    Actor* holder = registry.Resolve<Actor>(scene->actors[0]);
    REQUIRE(holder != nullptr);
    const Route* route = holder->GetComponent<Route>();
    REQUIRE(route != nullptr);
    CHECK(route->ids == std::vector<std::int32_t>{7, 0, 9}); // 読めなかった要素は既定の値
    CHECK(route->speeds.empty());
}

TEST_CASE("SF-2a: プレハブとの違いは、配列まるごとを1つの値として比べ、書き戻せる。参照の配列は比べない")
{
    husky::tests::TestFolder folder("husky_test_array_prefab");
    folder.Write("route.prefab", R"({ "version": 1, "objects": [
        { "id": 0, "type": "Actor", "properties": { "name": "Route", "components": [1] } },
        { "id": 1, "type": "Route", "properties": { "ids": [1, 2] } }
    ], "actors": [0] })");
    const husky::core::Guid guid = husky::tests::MakeGuid(0x8e1a2b3c4d5e4f60ull, 0x9a9b0c1d2e3f4a01ull);
    folder.Write("route.prefab.meta", husky::engine::AssetDatabase::MakeMetaText(guid));

    husky::engine::Application app;
    app.GetTypeRegistry().Register<Waypoint>();
    app.GetTypeRegistry().Register<Route>();
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const husky::engine::PrefabAsset* prefab = app.FindPrefabForScene(guid);
    REQUIRE(prefab != nullptr);
    const husky::engine::Prefab::InstantiateResult placed =
        husky::engine::Prefab::Instantiate(*prefab, app.GetWorld(), app.GetTypeRegistry());
    REQUIRE(placed.actor != nullptr);
    Route* route = placed.actor->GetComponent<Route>();
    REQUIRE(route != nullptr);
    CHECK(route->ids == std::vector<std::int32_t>{1, 2});
    const TypeRegistry& types = app.GetTypeRegistry();
    CHECK(husky::engine::Prefab::FindOverrides(*placed.actor, &types).overrides.empty());

    route->ids.push_back(5);
    route->points.resize(1);
    route->points[0].Set(app.GetWorld().Spawn<Waypoint>()); // 参照の配列は違いに数えない(ハンドルと同じ)
    const husky::engine::Prefab::OverridesResult found = husky::engine::Prefab::FindOverrides(*placed.actor, &types);
    REQUIRE(found.overrides.size() == 1);
    CHECK(found.overrides[0].component == "Route");
    CHECK(found.overrides[0].path == "ids"); // 配列は、まるごと1つの値

    husky::engine::Prefab::ValueChange change;
    change.component = found.overrides[0].component;
    change.componentId = found.overrides[0].componentId;
    change.path = found.overrides[0].path;
    change.oldValue = found.overrides[0].prefabValue;
    change.newValue = found.overrides[0].value;
    ArchiveValue written;
    std::string error;
    REQUIRE(husky::engine::Prefab::WriteChanges(prefab->GetData(), {change}, written, error));
    const husky::engine::Prefab::InstantiateResult again = husky::engine::Prefab::Instantiate(written, app.GetWorld(), types);
    REQUIRE(again.actor != nullptr);
    CHECK(again.actor->GetComponent<Route>()->ids == std::vector<std::int32_t>{1, 2, 5});
}

TEST_CASE("SF-2a: std::vector<bool> は要素の場所が取れないので、写して(load)触って戻す(store)形で読み書きする")
{
    const husky::core::Property* flags = Route::StaticType().FindProperty("flags");
    REQUIRE(flags != nullptr);
    REQUIRE(flags->GetArrayInfo() != nullptr);
    CHECK(flags->GetArrayInfo()->IsProxy());
    CHECK(flags->GetArrayInfo()->element.GetType() == PropertyType::Bool);
    CHECK_FALSE(Route::StaticType().FindProperty("ids")->GetArrayInfo()->IsProxy());

    // 読めない要素は false のまま、ほかは読む
    const TypeRegistry types = MakeTypes();
    ArchiveValue file;
    REQUIRE(husky::engine::JsonFormat::Read(R"({ "version": 1, "objects": [
        { "id": 0, "type": "Actor", "properties": { "name": "Holder", "components": [1] } },
        { "id": 1, "type": "Route", "properties": { "flags": [true, 3, true] } }
    ], "actors": [0] })", file));
    ObjectRegistry registry;
    World* scene = CreateRootScene(registry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("[1]") != std::string::npos);
    const Route* route = registry.Resolve<Actor>(scene->actors[0])->GetComponent<Route>();
    REQUIRE(route != nullptr);
    CHECK(route->flags == std::vector<bool>{true, false, true});
    registry.CollectGarbage(); // GC は bool の配列に何もしない(落ちない)
}
