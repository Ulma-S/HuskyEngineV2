// Rigidbody2D(物理の体のコンポーネント)と、PhysicsScene2D の両方向の同期のテスト(PHY-2a)。
// Application::StepFrame を決まった dt(1/60 秒 = 物理の 1 歩)で回して確かめる。
#include "engine/Rigidbody2D.h"

#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/Collider2D.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"
#include "engine/Scene.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::BodyType2D;
using husky::engine::BoxCollider2D;
using husky::engine::Rigidbody2D;

namespace
{
    // 32 x 32 の四角のコライダーと、bodyType の Rigidbody2D を持つアクターを置く。
    Actor* SpawnBody(Application& app, float x, float y, BodyType2D type)
    {
        Actor* actor = app.GetScene().Spawn<Actor>();
        actor->transform.position = {x, y, 0.0f};
        actor->AddComponent<Rigidbody2D>()->bodyType = type;
        actor->AddComponent<BoxCollider2D>();
        return actor;
    }

    void Steps(Application& app, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            app.StepFrame(0, {});
        }
    }

    void SetupApp(Application& app, float gravityY)
    {
        app.GetClock().SetFixedDelta(1.0 / 60.0);
        app.GetPhysics2D().GetWorld().SetGravity({0.0f, gravityY});
    }
}

TEST_CASE("PHY-2a: Dynamic の体は重力で落ち、落ちた位置がアクターの transform に書き戻される")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 60); // 1 秒
    CHECK(app.GetPhysics2D().GetBodyCount() == 1);
    CHECK(app.GetPhysics2D().GetColliderCount() == 1);
    // 1 秒で、およそ 1/2 × 1000 × 1² = 500 落ちる(1 歩ずつの計算なので少しずれる)
    CHECK(actor->transform.position.y < -450.0f);
    CHECK(actor->transform.position.y > -560.0f);
    CHECK(actor->transform.position.x == doctest::Approx(0.0f));
}

TEST_CASE("PHY-2a: Dynamic の体は Static の床の上で止まる。Static はアクターの位置のまま動かない")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* floor = SpawnBody(app, 0.0f, -100.0f, BodyType2D::Static);
    floor->GetComponent<BoxCollider2D>()->size = {400.0f, 32.0f};
    Actor* box = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 120);
    // 床の上面 -84 + 箱の半分 16 = -68 で止まる(押し合いの許し分だけ少し沈む)
    CHECK(box->transform.position.y == doctest::Approx(-68.0f).epsilon(0.02));
    CHECK(floor->transform.position.y == -100.0f);
}

TEST_CASE("PHY-2a: コードがアクターの位置を書き換えると、瞬間移動として体に伝わる")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 2);
    actor->transform.position = {500.0f, 300.0f, 0.0f};
    actor->transform.rotation = 30.0f;
    Steps(app, 1);
    // 重力 0・速度 0 なので、書いた場所のまま(体が古い場所に戻していない)
    CHECK(actor->transform.position.x == doctest::Approx(500.0f));
    CHECK(actor->transform.position.y == doctest::Approx(300.0f));
    CHECK(actor->transform.rotation == doctest::Approx(30.0f));
    // 当たり判定の世界でも、新しい場所にある
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(500.0f, 300.0f, 0xFFFFFFFFu, hits);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner == actor->GetComponent<BoxCollider2D>()->GetHandle());
}

TEST_CASE("PHY-2a: 書き戻しでもアクターの角度は丸めない(370 度は 370 度のまま)")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    actor->transform.rotation = 370.0f;
    Steps(app, 10);
    CHECK(actor->transform.rotation == doctest::Approx(370.0f));
}

TEST_CASE("PHY-2a: Kinematic の体はコードが動かす。物理は位置を書き戻さない")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Kinematic);
    Steps(app, 30);
    CHECK(actor->transform.position.y == 0.0f); // 重力を受けない
    actor->transform.position.x = 200.0f;
    Steps(app, 1);
    CHECK(actor->transform.position.x == 200.0f);
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(200.0f, 0.0f, 0xFFFFFFFFu, hits);
    CHECK(hits.size() == 1);
}

TEST_CASE("PHY-2a: Rigidbody2D を外すと体が消え、コライダーは形だけの体に置き直される(もう落ちない)")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 10);
    REQUIRE(app.GetPhysics2D().GetBodyCount() == 1);
    CHECK(actor->RemoveComponent(*actor->GetComponent<Rigidbody2D>()) == 0);
    Steps(app, 1);
    CHECK(app.GetPhysics2D().GetBodyCount() == 0);
    CHECK(app.GetPhysics2D().GetWorld().GetBodyCount() == 0); // 世界からも体が消えた
    CHECK(app.GetPhysics2D().GetColliderCount() == 1);
    const float y = actor->transform.position.y;
    Steps(app, 10);
    CHECK(actor->transform.position.y == y);
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(0.0f, y, 0xFFFFFFFFu, hits);
    CHECK(hits.size() == 1); // 当たり判定は残る
}

TEST_CASE("PHY-2a: 体の種類を変えると置き直される(Dynamic → Static で止まる)。アクターを消すと体も消える")
{
    Application app;
    SetupApp(app, -1000.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 10);
    actor->GetComponent<Rigidbody2D>()->bodyType = BodyType2D::Static;
    Steps(app, 1);
    const float y = actor->transform.position.y;
    Steps(app, 10);
    CHECK(actor->transform.position.y == y);
    CHECK(app.GetPhysics2D().GetWorld().GetBodyCount() == 1);
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(0.0f, y, 0xFFFFFFFFu, hits);
    CHECK(hits.size() == 1); // コライダーは新しい体に付け直された

    actor->Destroy();
    Steps(app, 2); // 1 フレーム目の終わりに消え、2 フレーム目の同期で世界から外れる
    CHECK(app.GetPhysics2D().GetBodyCount() == 0);
    CHECK(app.GetPhysics2D().GetWorld().GetBodyCount() == 0);
    CHECK(app.GetPhysics2D().GetColliderCount() == 0);
}

TEST_CASE("PHY-2a: 体に付けたコライダーどうしも重なりのイベントが届く")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* a = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Kinematic);
    a->GetComponent<BoxCollider2D>()->isTrigger = true; // isTrigger はまだ覚えるだけ(PHY-3)
    Actor* b = app.GetScene().Spawn<Actor>();
    b->transform.position = {10.0f, 0.0f, 0.0f};
    b->AddComponent<BoxCollider2D>();
    Steps(app, 1);
    // イベントの中身は COL-3 のテストで確かめているので、ここでは両方の形が置かれていることだけを見る
    CHECK(app.GetPhysics2D().GetColliderCount() == 2);
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(5.0f, 0.0f, 0xFFFFFFFFu, hits);
    CHECK(hits.size() == 2);
}

TEST_CASE("PHY-2a: Rigidbody2D の体の種類は名前で保存され、読み直すと同じ値になる")
{
    Application app;
    Actor* actor = app.GetScene().Spawn<Actor>();
    Rigidbody2D* source = actor->AddComponent<Rigidbody2D>();
    source->bodyType = BodyType2D::Kinematic;
    source->gravityScale = 0.5f;
    source->fixedRotation = true;

    std::string text;
    REQUIRE(husky::engine::JsonFormat::Write(husky::engine::ObjectSerializer::Write(*source), text));
    CHECK(text.find("\"Kinematic\"") != std::string::npos);

    husky::engine::ArchiveValue value;
    REQUIRE(husky::engine::JsonFormat::Read(text, value));
    Rigidbody2D* loaded = actor->AddComponent<Rigidbody2D>();
    CHECK(husky::engine::ObjectSerializer::ReadProperties(value, *loaded).warnings.empty());
    CHECK(loaded->bodyType == BodyType2D::Kinematic);
    CHECK(loaded->gravityScale == 0.5f);
    CHECK(loaded->fixedRotation);
}
