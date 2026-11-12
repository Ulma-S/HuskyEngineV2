// SceneSerializer(シーン全体の保存と読み込み、ハンドルの付け替え)のテスト。
// 保存したシーンを、別のレジストリ(= 別のメモリの状態)に読み込んで、同じ形に戻るかを確かめる。
#include "engine/EngineTypes.h"
#include "engine/JsonFormat.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteComponent.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;
using husky::core::TypeRegistry;
using husky::engine::Actor;
using husky::engine::ArchiveValue;
using husky::engine::Scene;
using husky::engine::SpriteComponent;
namespace JsonFormat = husky::engine::JsonFormat;
namespace SceneSerializer = husky::engine::SceneSerializer;

namespace
{
    // ほかのアクターを狙う(ハンドルのプロパティを持つ)アクター。
    class Hunter : public Actor
    {
        HUSKY_OBJECT(Hunter, Actor)
    public:
        std::int32_t level = 1;
        ObjectHandle target;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(level)
            HUSKY_PROPERTY(target)
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
        types.Register<Hunter>();
        return types;
    }

    // 保存 → JSON の文字列 → 読み込み、を通す(ファイルに書いて読むのと同じ)。
    ArchiveValue ThroughJson(const ArchiveValue& value)
    {
        std::string text;
        REQUIRE(JsonFormat::Write(value, text));
        ArchiveValue loaded;
        REQUIRE(JsonFormat::Read(text, loaded));
        return loaded;
    }
}

TEST_CASE("SceneSerializer: 保存したシーンを別のレジストリに読み込むと、アクター、コンポーネント、値、参照が同じ形に戻る")
{
    const TypeRegistry types = MakeTypes();

    // --- 保存する側 ---
    ObjectRegistry sourceRegistry;
    Scene* source = CreateRootScene(sourceRegistry);
    Actor* prey = source->Spawn<Actor>();
    prey->name = "うさぎ";
    prey->transform.position.x = 5.5f;
    SpriteComponent* preySprite = prey->AddComponent<SpriteComponent>();
    preySprite->width = 16.0f;
    preySprite->color = {1.0f, 0.5f, 0.25f, 0.75f}; // 構造体(render::Color)のプロパティ
    Hunter* hunter = source->Spawn<Hunter>();
    hunter->name = "きつね";
    hunter->level = 7;
    hunter->target = prey->GetHandle(); // 先に保存される相手を指す
    hunter->AddComponent<SpriteComponent>()->height = 64.0f;

    const SceneSerializer::WriteResult saved = SceneSerializer::Write(*source);
    CHECK(saved.warnings.empty());
    const ArchiveValue file = ThroughJson(saved.value);

    // --- 読み込む側(別のレジストリ。ハンドルの値は保存した側と無関係) ---
    ObjectRegistry targetRegistry;
    targetRegistry.Create<Actor>(); // わざと1つ作っておき、ハンドルの場所をずらす
    Scene* target = CreateRootScene(targetRegistry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *target, types);

    CHECK(report.warnings.empty());
    CHECK(report.createdCount == 4);    // アクター 2 + コンポーネント 2
    CHECK(report.addedActorCount == 2);
    REQUIRE(target->actors.size() == 2);

    Actor* loadedPrey = targetRegistry.Resolve<Actor>(target->actors[0]);
    Hunter* loadedHunter = targetRegistry.Resolve<Hunter>(target->actors[1]); // 型も Hunter として作られている
    REQUIRE(loadedPrey != nullptr);
    REQUIRE(loadedHunter != nullptr);
    CHECK(loadedPrey->name == "うさぎ");
    CHECK(loadedPrey->transform.position.x == 5.5f);
    CHECK(loadedHunter->level == 7);

    // 参照: target は、読み込んだ側の「うさぎ」を指すように付け替えられている
    CHECK(loadedHunter->target == loadedPrey->GetHandle());
    CHECK(loadedHunter->target != prey->GetHandle()); // 保存した側のハンドルのままではない

    // コンポーネント: 値と、持ち主とシーンの付け直し
    SpriteComponent* loadedSprite = loadedPrey->GetComponent<SpriteComponent>();
    REQUIRE(loadedSprite != nullptr);
    CHECK(loadedSprite->width == 16.0f);
    CHECK(loadedSprite->color.g == 0.5f); // 構造体の中のメンバーも戻る
    CHECK(loadedSprite->color.b == 0.25f);
    CHECK(loadedSprite->color.a == 0.75f);
    CHECK(loadedSprite->GetOwner() == loadedPrey);
    CHECK(loadedHunter->GetComponent<SpriteComponent>()->height == 64.0f);
    CHECK(loadedPrey->GetScene() == target);

    // シーンが根なので、読み込んだものは GC で回収されない(最初にわざと作った1つだけが回収される)
    CHECK(targetRegistry.CollectGarbage() == 1);
    CHECK(targetRegistry.Resolve(loadedSprite->GetHandle()) == loadedSprite);
}

TEST_CASE("SceneSerializer: Destroy 済みのアクターとコンポーネントは保存しない")
{
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* kept = scene->Spawn<Actor>();
    kept->AddComponent<SpriteComponent>()->Destroy();
    scene->Spawn<Actor>()->Destroy();

    const SceneSerializer::WriteResult saved = SceneSerializer::Write(*scene);

    CHECK(saved.value.Find("objects")->GetArraySize() == 1); // kept だけ
    CHECK(saved.value.Find("actors")->GetArraySize() == 1);
    // kept の components には、保存しなかったコンポーネントのハンドルが残っているので、null にした旨が出る
    CHECK(saved.warnings.size() == 1);
}

TEST_CASE("SceneSerializer: シーンの外のオブジェクトを指すハンドルは null で保存し、読み込むとどこも指さない")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Hunter* hunter = scene->Spawn<Hunter>();
    hunter->target = registry.Create<Actor>()->GetHandle(); // シーンにいないアクター

    const SceneSerializer::WriteResult saved = SceneSerializer::Write(*scene);
    CHECK(saved.warnings.size() == 1);

    ObjectRegistry otherRegistry;
    Scene* other = CreateRootScene(otherRegistry);
    SceneSerializer::Read(ThroughJson(saved.value), *other, types);
    Hunter* loaded = otherRegistry.Resolve<Hunter>(other->actors.at(0));
    REQUIRE(loaded != nullptr);
    CHECK(loaded->target.IsNull());
}

TEST_CASE("SceneSerializer: 知らない型は作らずに飛ばし、それを指すハンドルはどこも指さない")
{
    TypeRegistry types; // Hunter を登録していない
    husky::engine::RegisterEngineTypes(types);

    ArchiveValue file;
    REQUIRE(JsonFormat::Read(R"({
        "version": 1,
        "objects": [
            { "id": 0, "type": "Actor", "properties": { "name": "のこる", "components": [1, 2] } },
            { "id": 1, "type": "Mystery", "properties": {} },
            { "id": 2, "type": "SpriteComponent", "properties": { "width": 8 } }
        ],
        "actors": [0, 1]
    })", file));

    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);

    CHECK(report.createdCount == 2);
    CHECK(report.addedActorCount == 1);
    REQUIRE(scene->actors.size() == 1);
    Actor* actor = registry.Resolve<Actor>(scene->actors[0]);
    REQUIRE(actor != nullptr);
    CHECK(actor->components.size() == 1); // 作れなかった 1 番は抜けて、2 番だけが残る
    CHECK(actor->GetComponent<SpriteComponent>()->width == 8.0f);
    // 警告: Mystery を作れない、components の 1 番が無い、actors の 1 番を入れられない
    CHECK(report.warnings.size() == 3);
}

TEST_CASE("SceneSerializer: 今いるアクターは消さず、読み込んだアクターを後ろに足す")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    scene->Spawn<Actor>()->name = "まえから";
    const ArchiveValue file = ThroughJson(SceneSerializer::Write(*scene).value);

    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types); // 同じシーンに読み込む

    CHECK(report.addedActorCount == 1);
    REQUIRE(scene->actors.size() == 2);
    CHECK(registry.Resolve<Actor>(scene->actors[1])->name == "まえから");
    CHECK(scene->actors[0] != scene->actors[1]); // 別のオブジェクトとして作られている
}

TEST_CASE("Actor の guid: シーンに入れると振られ、保存して読み戻しても同じ。名前が重なっても guid で区別できる")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* first = scene->Spawn<Actor>();
    Actor* second = scene->Spawn<Actor>();
    first->name = "Enemy";
    second->name = "Enemy"; // 名前は重なってよい
    CHECK_FALSE(first->guid.IsNull());
    CHECK(first->guid != second->guid);
    CHECK(scene->FindActorByGuid(second->guid) == second);
    CHECK(scene->FindActorByGuid(husky::core::Guid{}) == nullptr);

    // 保存して別のレジストリに読み戻しても、同じ guid
    const ArchiveValue file = ThroughJson(SceneSerializer::Write(*scene).value);
    ObjectRegistry otherRegistry;
    Scene* other = CreateRootScene(otherRegistry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *other, types);
    CHECK(report.warnings.empty());
    REQUIRE(other->actors.size() == 2);
    const Actor* loaded = other->FindActorByGuid(second->guid);
    REQUIRE(loaded != nullptr);
    CHECK(loaded == otherRegistry.Resolve<Actor>(other->actors[1]));

    // Destroy したアクターは、guid で探しても見つからない
    second->Destroy();
    CHECK(scene->FindActorByGuid(second->guid) == nullptr);
}

TEST_CASE("Actor の guid: 読み込んだアクターの guid がシーンの中で重なったら、後のほうに新しい guid を振り、警告を出す")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    Actor* original = scene->Spawn<Actor>();
    const husky::core::Guid originalGuid = original->guid;
    const ArchiveValue file = ThroughJson(SceneSerializer::Write(*scene).value);

    // 同じファイルを、同じシーンにもう一度読む(手でコピーしたのと同じ): 前からいるほうの guid は変えない
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);
    REQUIRE(scene->actors.size() == 2);
    const Actor* copy = registry.Resolve<Actor>(scene->actors[1]);
    CHECK(original->guid == originalGuid);
    CHECK(copy->guid != originalGuid);
    CHECK_FALSE(copy->guid.IsNull());
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("guid") != std::string::npos);

    // Destroy 済みのアクター(Play / Stop で消す途中のものなど)とは、重なりとして扱わない
    original->Destroy();
    copy = nullptr;
    registry.Resolve<Actor>(scene->actors[1])->Destroy();
    const SceneSerializer::ReadReport again = SceneSerializer::Read(file, *scene, types);
    CHECK(again.warnings.empty());
    CHECK(scene->FindActorByGuid(originalGuid) != nullptr);
}

TEST_CASE("Actor の guid: guid の無い古いファイルも読め、読んだときに振られる")
{
    const TypeRegistry types = MakeTypes();
    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    ArchiveValue file;
    REQUIRE(JsonFormat::Read(R"({ "version": 1, "objects": [ { "id": 0, "type": "Actor", "properties": { "name": "old" } } ],
                                  "actors": [0] })",
                             file));
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);
    CHECK(report.warnings.empty());
    REQUIRE(scene->actors.size() == 1);
    CHECK_FALSE(registry.Resolve<Actor>(scene->actors[0])->guid.IsNull());
}

TEST_CASE("SceneSerializer: 版が無いファイルは警告を出して、読めるところまで読む")
{
    const TypeRegistry types = MakeTypes();
    ArchiveValue file;
    REQUIRE(JsonFormat::Read(R"({ "objects": [ { "id": 0, "type": "Actor", "properties": {} } ], "actors": [0] })", file));

    ObjectRegistry registry;
    Scene* scene = CreateRootScene(registry);
    const SceneSerializer::ReadReport report = SceneSerializer::Read(file, *scene, types);

    CHECK(report.addedActorCount == 1);
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("版") != std::string::npos);
}
