// アクターの親子の階層のテスト(HIER-1)。
// transform は親から見たローカルの値で、ワールドの値は「親のワールドの行列 × 自分のローカルの行列」から計算する。
// 描画・当たり判定・選択がワールドの値を使うことも確かめる。
#include "engine/Actor.h"

#include "engine/Application.h"
#include "engine/Collider2D.h"
#include "engine/Rigidbody2D.h"
#include "engine/World.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteComponent.h"
#include "engine/TransformMatrix.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using husky::core::Vector3;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::Transform;

namespace
{
    Actor* SpawnAt(Application& app, float x, float y)
    {
        Actor* actor = app.GetWorld().Spawn<Actor>();
        actor->transform.position = {x, y, 0.0f};
        return actor;
    }

    void CheckNear(const Vector3& actual, float x, float y)
    {
        CHECK(actual.x == doctest::Approx(x).epsilon(1e-4));
        CHECK(actual.y == doctest::Approx(y).epsilon(1e-4));
    }
}

TEST_CASE("HIER-1: DecomposeModelMatrix は MakeModelMatrix の値に戻す(反転は scale.y のマイナス)")
{
    Transform original;
    original.position = {10.0f, -20.0f, 3.0f};
    original.rotation = 30.0f;
    original.scale = {2.0f, 0.5f, 1.0f};
    const Transform back = husky::engine::DecomposeModelMatrix(husky::engine::MakeModelMatrix(original));
    CheckNear(back.position, 10.0f, -20.0f);
    CHECK(back.position.z == doctest::Approx(3.0f));
    CHECK(back.rotation == doctest::Approx(30.0f));
    CHECK(back.scale.x == doctest::Approx(2.0f));
    CHECK(back.scale.y == doctest::Approx(0.5f));

    // y の反転は、そのまま scale.y のマイナスに戻る
    Transform flipped;
    flipped.scale = {1.0f, -1.0f, 1.0f};
    const Transform flippedBack = husky::engine::DecomposeModelMatrix(husky::engine::MakeModelMatrix(flipped));
    CHECK(flippedBack.rotation == doctest::Approx(0.0f));
    CHECK(flippedBack.scale.y == doctest::Approx(-1.0f));
}

TEST_CASE("HIER-1: 親が無ければワールドの値は transform そのもの。子のワールドの値は親の回転と拡大縮小を通る")
{
    Application app;
    Actor* parent = SpawnAt(app, 100.0f, 0.0f);
    parent->transform.rotation = 90.0f;
    parent->transform.scale = {2.0f, 2.0f, 1.0f};
    Actor* child = SpawnAt(app, 10.0f, 0.0f);
    child->transform.rotation = 15.0f;

    // 親なし: 分け直さないので、角度 370 度もそのまま
    parent->transform.rotation = 370.0f;
    CHECK(parent->GetWorldTransform().rotation == 370.0f);
    parent->transform.rotation = 90.0f;

    REQUIRE(child->SetParent(parent, false)); // transform (10, 0) を、そのまま親から見た値として使う
    CHECK(child->GetParent() == parent);
    std::vector<Actor*> children;
    parent->GetChildren(children);
    REQUIRE(children.size() == 1);
    CHECK(children[0] == child);

    // (10, 0) を 2 倍して 90 度回すと (0, 20)。親の位置 (100, 0) を足して (100, 20)
    CheckNear(child->GetWorldPosition(), 100.0f, 20.0f);
    const Transform world = child->GetWorldTransform();
    CheckNear(world.position, 100.0f, 20.0f);
    CHECK(world.rotation == doctest::Approx(105.0f));
    CHECK(world.scale.x == doctest::Approx(2.0f));
    CHECK(world.scale.y == doctest::Approx(2.0f));
    CHECK(child->transform.position == (Vector3{10.0f, 0.0f, 0.0f})); // ローカルの値は変わらない

    // 孫: 親の親までたどる
    Actor* grandchild = SpawnAt(app, 5.0f, 0.0f);
    REQUIRE(grandchild->SetParent(child, false));
    // 子の向き(105 度)と大きさ(2 倍)で (5, 0) → 長さ 10 の 105 度の向き
    const Vector3 expected = {100.0f + 10.0f * std::cos(105.0f * 3.14159265f / 180.0f),
                              20.0f + 10.0f * std::sin(105.0f * 3.14159265f / 180.0f), 0.0f};
    CheckNear(grandchild->GetWorldPosition(), expected.x, expected.y);
}

TEST_CASE("HIER-1: SetParent は既定で見える場所(ワールドの値)を保ち、親を外しても保つ")
{
    Application app;
    Actor* parent = SpawnAt(app, 100.0f, 50.0f);
    parent->transform.rotation = 90.0f;
    Actor* child = SpawnAt(app, 120.0f, 50.0f);
    child->transform.rotation = 30.0f;

    REQUIRE(child->SetParent(parent));
    CheckNear(child->GetWorldPosition(), 120.0f, 50.0f);
    CHECK(child->GetWorldTransform().rotation == doctest::Approx(30.0f));
    // 親から見ると: 右へ 20 は、90 度回った親の「下へ 20」(-y)。向きは 30 - 90 = -60 度
    CheckNear(child->transform.position, 0.0f, -20.0f);
    CHECK(child->transform.rotation == doctest::Approx(-60.0f));

    // 親を動かすと子もついて行く
    parent->transform.position.x += 10.0f;
    CheckNear(child->GetWorldPosition(), 130.0f, 50.0f);

    // ワールドの位置を書くと、親から見た値に直して入る
    child->SetWorldPosition({110.0f, 60.0f, 0.0f});
    CheckNear(child->GetWorldPosition(), 110.0f, 60.0f);
    CheckNear(child->transform.position, 10.0f, 0.0f);

    // ワールドの値をまとめて書く
    Transform world = child->GetWorldTransform();
    world.rotation = 45.0f;
    child->SetWorldTransform(world);
    CHECK(child->GetWorldTransform().rotation == doctest::Approx(45.0f));
    CheckNear(child->GetWorldPosition(), 110.0f, 60.0f);

    // 親を外す: 見える場所のまま、transform がワールドの値になる
    REQUIRE(child->SetParent(nullptr));
    CHECK(child->GetParent() == nullptr);
    std::vector<Actor*> children;
    parent->GetChildren(children);
    CHECK(children.empty());
    CheckNear(child->transform.position, 110.0f, 60.0f);
    CHECK(child->transform.rotation == doctest::Approx(45.0f));
}

TEST_CASE("HIER-1: 輪になる親と、Rigidbody2D を持つアクターの親は断る")
{
    Application app;
    Actor* a = SpawnAt(app, 0.0f, 0.0f);
    Actor* b = SpawnAt(app, 0.0f, 0.0f);
    Actor* c = SpawnAt(app, 0.0f, 0.0f);
    CHECK_FALSE(a->SetParent(a)); // 自分
    REQUIRE(b->SetParent(a));
    REQUIRE(c->SetParent(b));
    CHECK_FALSE(a->SetParent(c)); // 孫を親にすると輪になる
    CHECK(a->GetParent() == nullptr);

    // 付け替え: 前の親の子の一覧から外れる
    REQUIRE(c->SetParent(a));
    std::vector<Actor*> children;
    b->GetChildren(children);
    CHECK(children.empty());
    a->GetChildren(children);
    CHECK(children.size() == 2);

    Actor* body = SpawnAt(app, 0.0f, 0.0f);
    body->AddComponent<husky::engine::Rigidbody2D>();
    CHECK_FALSE(body->SetParent(a));
    CHECK(body->GetParent() == nullptr);
}

TEST_CASE("HIER-2a: 親を消すと、子孫もフレームの終わりに一緒に消える")
{
    Application app;
    Actor* parent = SpawnAt(app, 0.0f, 0.0f);
    Actor* child = SpawnAt(app, 0.0f, 0.0f);
    Actor* grandchild = SpawnAt(app, 0.0f, 0.0f);
    Actor* other = SpawnAt(app, 0.0f, 0.0f);
    // 一覧の順番に関係なく消える(孫を親より前に置いた形も試す)
    REQUIRE(grandchild->SetParent(child));
    REQUIRE(child->SetParent(parent));
    REQUIRE(other->SetParent(child) == true);
    REQUIRE(other->SetParent(nullptr));

    parent->Destroy();
    app.StepFrame(0, {});
    CHECK(app.GetWorld().actors.size() == 1);
    CHECK(app.GetWorld().FindActorByGuid(other->guid) == other);
}

TEST_CASE("HIER-2a: 親は保存され、読み戻すとつながる。子孫は親が先の順で集まる")
{
    Application app;
    Actor* parent = SpawnAt(app, 100.0f, 0.0f);
    parent->name = "Parent";
    Actor* child = SpawnAt(app, 10.0f, 0.0f);
    child->name = "Child";
    Actor* grandchild = SpawnAt(app, 1.0f, 0.0f);
    grandchild->name = "Grandchild";
    REQUIRE(grandchild->SetParent(child, false));
    REQUIRE(child->SetParent(parent, false));

    std::vector<Actor*> descendants;
    parent->GetDescendants(descendants);
    REQUIRE(descendants.size() == 2);
    CHECK(descendants[0] == child);
    CHECK(descendants[1] == grandchild);

    const husky::engine::SceneSerializer::WriteResult saved = husky::engine::SceneSerializer::Write(app.GetWorld());
    CHECK(saved.warnings.empty());
    Application loaded;
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved.value, loaded.GetWorld(), loaded.GetTypeRegistry());
    CHECK(report.warnings.empty());
    Actor* loadedGrandchild = loaded.GetWorld().FindActorByGuid(grandchild->guid);
    REQUIRE(loadedGrandchild != nullptr);
    REQUIRE(loadedGrandchild->GetParent() != nullptr);
    CHECK(loadedGrandchild->GetParent()->name == "Child");
    CHECK(loadedGrandchild->GetParent()->GetParent()->name == "Parent");
    CheckNear(loadedGrandchild->GetWorldPosition(), 111.0f, 0.0f);
}

TEST_CASE("HIER-2a: 読み込みで親子が輪になっていれば、親なしにして警告する")
{
    Application app;
    Actor* a = SpawnAt(app, 0.0f, 0.0f);
    Actor* b = SpawnAt(app, 0.0f, 0.0f);
    REQUIRE(b->SetParent(a));
    husky::engine::SceneSerializer::WriteResult saved = husky::engine::SceneSerializer::Write(app.GetWorld());
    // 手で直したファイルのつもりで、a の親を b にする(番号は b の id)
    const husky::engine::ArchiveValue& objects = *saved.value.Find("objects");
    std::int64_t idOfB = -1;
    std::size_t indexOfA = 0;
    for (std::size_t i = 0; i < objects.GetArraySize(); ++i)
    {
        const husky::engine::ArchiveValue* properties = objects.At(i).Find("properties");
        const husky::engine::ArchiveValue* guid = properties != nullptr ? properties->Find("guid") : nullptr;
        if (guid != nullptr && guid->AsString() == b->guid.ToString())
        {
            idOfB = objects.At(i).Find("id")->AsInt();
        }
        if (guid != nullptr && guid->AsString() == a->guid.ToString())
        {
            indexOfA = i;
        }
    }
    REQUIRE(idOfB >= 0);
    husky::engine::ArchiveValue edited = husky::engine::ArchiveValue::MakeArray();
    for (std::size_t i = 0; i < objects.GetArraySize(); ++i)
    {
        husky::engine::ArchiveValue object = objects.At(i);
        if (i == indexOfA)
        {
            husky::engine::ArchiveValue properties = *object.Find("properties");
            properties.Set("parent", husky::engine::ArchiveValue(idOfB));
            object.Set("properties", std::move(properties));
        }
        edited.Append(std::move(object));
    }
    saved.value.Set("objects", std::move(edited));

    Application loaded;
    const husky::engine::SceneSerializer::ReadReport report =
        husky::engine::SceneSerializer::Read(saved.value, loaded.GetWorld(), loaded.GetTypeRegistry());
    CHECK(report.warnings.size() == 1);
    Actor* loadedA = loaded.GetWorld().FindActorByGuid(a->guid);
    Actor* loadedB = loaded.GetWorld().FindActorByGuid(b->guid);
    REQUIRE(loadedA != nullptr);
    REQUIRE(loadedB != nullptr);
    CHECK_FALSE(loadedA->IsDescendantOf(*loadedA));
    CHECK_FALSE(loadedB->IsDescendantOf(*loadedB));
}

TEST_CASE("HIER-1: 描画・選択・当たり判定はワールドの値を使う")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    Actor* parent = SpawnAt(app, 200.0f, 100.0f);
    parent->transform.rotation = 90.0f;
    Actor* child = SpawnAt(app, 50.0f, 0.0f);
    REQUIRE(child->SetParent(parent, false)); // ワールドでは (200, 150)
    husky::engine::SpriteComponent* sprite = child->AddComponent<husky::engine::SpriteComponent>();
    sprite->width = 20.0f;
    sprite->height = 20.0f;
    child->AddComponent<husky::engine::BoxCollider2D>()->size = {20.0f, 20.0f};

    // 描画: モデル行列はワールドの行列
    std::vector<husky::render::Sprite> sprites;
    husky::engine::ForEachSprite(app.GetWorld(),
                                 [&](husky::render::Sprite& drawn, husky::engine::SpriteComponent&) { sprites.push_back(drawn); });
    REQUIRE(sprites.size() == 1);
    CHECK(sprites[0].model == child->GetWorldMatrix());

    // 選択: ワールドの位置で当たる(ローカルの値 (50, 0) では当たらない)
    CHECK(husky::engine::FindSpriteActorAt(app.GetWorld(), 200.0f, 150.0f) == child);
    CHECK(husky::engine::FindSpriteActorAt(app.GetWorld(), 50.0f, 0.0f) == nullptr);

    // 当たり判定: 形はワールドの位置に置かれ、親を動かすとついて行く
    app.StepFrame(0, {});
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(200.0f, 150.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.size() == 1);
    parent->transform.position.x = 400.0f;
    app.StepFrame(0, {});
    app.GetPhysics2D().GetWorld().OverlapPoint(400.0f, 150.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.size() == 1);
    app.GetPhysics2D().GetWorld().OverlapPoint(200.0f, 150.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.empty());
}

TEST_CASE("HIER-1: 親を付けた後に足した Rigidbody2D は使わず、コライダーは形だけで置く")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    Actor* parent = SpawnAt(app, 100.0f, 0.0f);
    Actor* child = SpawnAt(app, 10.0f, 0.0f);
    REQUIRE(child->SetParent(parent, false));
    child->AddComponent<husky::engine::BoxCollider2D>();
    husky::engine::Rigidbody2D* body = child->AddComponent<husky::engine::Rigidbody2D>();
    app.StepFrame(0, {});
    app.StepFrame(0, {});
    CHECK_FALSE(body->HasBody());
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(110.0f, 0.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.size() == 1);
}
