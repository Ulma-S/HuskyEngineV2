// Rigidbody2D(物理の体のコンポーネント)と、PhysicsScene2D の両方向の同期のテスト(PHY-2a)。
// Application::StepFrame を決まった dt(1/60 秒 = 物理の 1 歩)で回して確かめる。
#include "engine/Rigidbody2D.h"

#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/Collider2D.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h"

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

TEST_CASE("PHY-2b: Kinematic をコードで動かすと、通り道の Dynamic を押す。Kinematic は書いた場所でぴったり止まる")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* pusher = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Kinematic);
    Actor* box = SpawnBody(app, 40.0f, 0.0f, BodyType2D::Dynamic); // 少しすき間をあけて右に置く
    Steps(app, 1);
    for (int i = 0; i < 30; ++i)
    {
        pusher->transform.position.x += 4.0f; // 1 フレームに 4 ずつ右へ(瞬間移動ではなく、速さを付けて動かす)
        Steps(app, 1);
    }
    CHECK(pusher->transform.position.x == 120.0f);
    // 押された箱は、押す側の右(120 + 32 = 152 くらい)より右にいる
    CHECK(box->transform.position.x > 145.0f);
    // 止めると、Kinematic は止まったまま(速さが 0 に戻っている)
    Steps(app, 5);
    CHECK(pusher->transform.position.x == 120.0f);
    CHECK(pusher->GetComponent<Rigidbody2D>()->GetLinearVelocity().x == 0.0f);
}

TEST_CASE("PHY-2b: 速度の関数。体ができる前に入れた速度は、体ができたときに入る。種類を変えても速さは引き継ぐ")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Rigidbody2D* body = actor->GetComponent<Rigidbody2D>();
    CHECK_FALSE(body->HasBody());
    body->SetLinearVelocity({120.0f, 0.0f}); // まだ体が無い
    CHECK(body->GetLinearVelocity().x == 120.0f);
    Steps(app, 60); // 1 秒
    CHECK(body->HasBody());
    CHECK(actor->transform.position.x == doctest::Approx(120.0f).epsilon(0.02));

    body->linearDamping = 0.5f; // 決め事を変えると体は置き直されるが、速さは残る
    Steps(app, 1);
    CHECK(body->GetLinearVelocity().x > 100.0f);
}

TEST_CASE("PHY-2b: 押す力(ApplyImpulse)と重さ。重さは 面積 × 密度(Collider2D::density)")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    actor->GetComponent<BoxCollider2D>()->density = 2.0f;
    Rigidbody2D* body = actor->GetComponent<Rigidbody2D>();
    CHECK(body->GetMass() == 0.0f); // 体が無い
    Steps(app, 1);
    CHECK(body->GetMass() == doctest::Approx(32.0f * 32.0f * 2.0f));
    body->ApplyImpulse({body->GetMass() * 60.0f, 0.0f}); // 速さ 60 にする
    CHECK(body->GetLinearVelocity().x == doctest::Approx(60.0f));
}

TEST_CASE("PHY-2b: 跳ね返り(restitution)が 1 なら、床で同じくらいの速さで跳ね返る")
{
    Application app;
    SetupApp(app, 0.0f);
    Actor* floor = SpawnBody(app, 0.0f, -100.0f, BodyType2D::Static);
    floor->GetComponent<BoxCollider2D>()->size = {400.0f, 32.0f};
    Actor* ball = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    ball->GetComponent<BoxCollider2D>()->restitution = 1.0f;
    Rigidbody2D* body = ball->GetComponent<Rigidbody2D>();
    body->SetLinearVelocity({0.0f, -300.0f});
    Steps(app, 30);
    CHECK(body->GetLinearVelocity().y > 250.0f); // 上向きに跳ね返った
}

namespace
{
    // 届いたぶつかり・重なりのイベントを覚えるアクター(PHY-3)。
    class ContactRecorder : public Actor
    {
        HUSKY_OBJECT_INLINE(ContactRecorder, Actor)
    public:
        void OnCollision(const husky::engine::CollisionEvent& event) override { collisions.push_back(event); }
        void OnOverlap(const husky::engine::OverlapEvent& event) override { overlaps.push_back(event); }
        std::vector<husky::engine::CollisionEvent> collisions;
        std::vector<husky::engine::OverlapEvent> overlaps;
    };

    // ぶつかりのイベントの数を数えるコンポーネント(コンポーネントにも届くことを確かめる)。
    class CollisionCounter : public husky::engine::Component
    {
        HUSKY_OBJECT_INLINE(CollisionCounter, husky::engine::Component)
    public:
        void OnCollision(const husky::engine::CollisionEvent& event) override { event.begin ? ++begins : ++ends; }
        int begins = 0;
        int ends = 0;
    };

    ContactRecorder* SpawnRecorder(Application& app, float x, float y, BodyType2D type)
    {
        ContactRecorder* actor = app.GetScene().Spawn<ContactRecorder>();
        actor->transform.position = {x, y, 0.0f};
        actor->AddComponent<Rigidbody2D>()->bodyType = type;
        actor->AddComponent<BoxCollider2D>();
        return actor;
    }
}

TEST_CASE("PHY-3: Dynamic が床に落ちると、両方に begin(向きは自分から相手へ)。重なりのイベントは届かない。離すと end")
{
    Application app;
    SetupApp(app, -1000.0f);
    ContactRecorder* floor = SpawnRecorder(app, 0.0f, -100.0f, BodyType2D::Static);
    floor->GetComponent<BoxCollider2D>()->size = {400.0f, 32.0f};
    ContactRecorder* box = SpawnRecorder(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    CollisionCounter* counter = box->AddComponent<CollisionCounter>();
    Steps(app, 60);
    REQUIRE(box->collisions.size() == 1);
    CHECK(box->collisions[0].begin);
    CHECK(box->collisions[0].otherActor == floor->GetHandle());
    CHECK(box->collisions[0].selfCollider == box->GetComponent<BoxCollider2D>()->GetHandle());
    CHECK(box->collisions[0].normal.y == doctest::Approx(-1.0f)); // 箱から床へ = 下向き
    REQUIRE(floor->collisions.size() == 1);
    CHECK(floor->collisions[0].otherActor == box->GetHandle());
    CHECK(floor->collisions[0].normal.y == doctest::Approx(1.0f)); // 床から箱へ = 上向き
    CHECK(counter->begins == 1);
    CHECK(box->overlaps.empty()); // 押し合う組は、重なりでは知らせない
    CHECK(floor->overlaps.empty());

    // 上へ瞬間移動させると離れる
    box->transform.position.y = 300.0f;
    Steps(app, 2);
    REQUIRE(box->collisions.size() == 2);
    CHECK_FALSE(box->collisions[1].begin);
    REQUIRE(floor->collisions.size() == 2);
    CHECK_FALSE(floor->collisions[1].begin);
    CHECK(counter->ends == 1);
}

TEST_CASE("PHY-3: トリガー(isTrigger)は押し合わず、すり抜けて重なりのイベントだけが届く")
{
    Application app;
    SetupApp(app, -1000.0f);
    ContactRecorder* floor = SpawnRecorder(app, 0.0f, -100.0f, BodyType2D::Static);
    floor->GetComponent<BoxCollider2D>()->size = {400.0f, 32.0f};
    floor->GetComponent<BoxCollider2D>()->isTrigger = true;
    ContactRecorder* box = SpawnRecorder(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 60);
    CHECK(box->transform.position.y < -200.0f); // 床で止まらずに落ちた
    CHECK(box->collisions.empty());
    CHECK(floor->collisions.empty());
    // 通り過ぎる間に、重なり始めて離れた
    REQUIRE(box->overlaps.size() == 2);
    CHECK(box->overlaps[0].begin);
    CHECK_FALSE(box->overlaps[1].begin);
}

TEST_CASE("PHY-3: ぶつかっている相手が消えると end が届く。体の種類を変えると、いったん離れてまたぶつかる")
{
    Application app;
    SetupApp(app, -1000.0f);
    ContactRecorder* floor = SpawnRecorder(app, 0.0f, -100.0f, BodyType2D::Static);
    floor->GetComponent<BoxCollider2D>()->size = {400.0f, 32.0f};
    ContactRecorder* box = SpawnRecorder(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    Steps(app, 60);
    REQUIRE(box->collisions.size() == 1);

    floor->GetComponent<Rigidbody2D>()->bodyType = BodyType2D::Kinematic; // 床の体を置き直す
    Steps(app, 2);
    REQUIRE(box->collisions.size() == 3);
    CHECK_FALSE(box->collisions[1].begin);
    CHECK(box->collisions[2].begin);

    const husky::core::ObjectHandle floorHandle = floor->GetHandle(); // 消えた後はポインタを触れないので、先に控える
    floor->Destroy();
    Steps(app, 2); // 1 フレーム目の終わりに消え、2 フレーム目の同期で世界から外れて end
    REQUIRE(box->collisions.size() == 4);
    CHECK_FALSE(box->collisions[3].begin);
    CHECK(box->collisions[3].otherActor == floorHandle);
}

TEST_CASE("PHY-4: 描くときは、前の歩と今の歩の間を、貯まった時間の割合で混ぜる(本当の transform は物理の位置のまま)")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 144.0); // 画面は 144 Hz、物理は 60 Hz(1 フレームに 0 歩か 1 歩)
    app.GetPhysics2D().GetWorld().SetGravity({0.0f, 0.0f});
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    actor->GetComponent<Rigidbody2D>()->SetLinearVelocity({60.0f, 0.0f}); // 1 歩で 1 単位進む
    Steps(app, 20);
    int zeroStepFrames = 0;
    for (int i = 0; i < 30; ++i)
    {
        Steps(app, 1);
        zeroStepFrames += app.GetPhysics2D().GetLastStepCount() == 0 ? 1 : 0;
        const float alpha = static_cast<float>(app.GetPhysics2D().GetStepper().GetAlpha());
        // 描く位置は、1 歩前(transform - 1)から今(transform)までの alpha の所
        CHECK(actor->GetRenderTransform().position.x == doctest::Approx(actor->transform.position.x - (1.0f - alpha)));
    }
    CHECK(zeroStepFrames > 0); // 物理が進まないフレームでも、描く位置は時間の分だけ進んでいた

    // 描くスプライトも、描くときの transform を使う
    actor->AddComponent<husky::engine::SpriteComponent>();
    Steps(app, 1);
    float drawnX = 0.0f;
    husky::engine::ForEachSprite(app.GetScene(), [&](const husky::render::Sprite& sprite, husky::engine::SpriteComponent&) {
        drawnX = sprite.model.m[12]; // 列優先なので、移動の x は 12 番目
    });
    CHECK(drawnX == doctest::Approx(actor->GetRenderTransform().position.x));
}

TEST_CASE("PHY-4: コードが動かしたら、すぐ本当の transform で描く。瞬間移動の後は、途中を混ぜない")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 144.0);
    app.GetPhysics2D().GetWorld().SetGravity({0.0f, 0.0f});
    Actor* actor = SpawnBody(app, 0.0f, 0.0f, BodyType2D::Dynamic);
    actor->GetComponent<Rigidbody2D>()->SetLinearVelocity({60.0f, 0.0f});
    Steps(app, 20);
    actor->transform.position = {500.0f, 0.0f, 0.0f};
    CHECK(actor->GetRenderTransform().position.x == 500.0f); // 入れた後に transform が変わったので、古い値は使わない
    Steps(app, 1);
    // 瞬間移動の直後は、前の歩(古い場所)と混ぜないので、0 近くからの途中を通って見えない
    CHECK(actor->GetRenderTransform().position.x > 498.0f);

    // Static の体と、体の無いアクターは、本当の transform で描く
    Actor* wall = SpawnBody(app, 100.0f, 0.0f, BodyType2D::Static);
    Steps(app, 3);
    CHECK(wall->GetRenderTransform().position.x == 100.0f);
    // Rigidbody2D を外すと、描くのも本当の transform に戻る
    actor->RemoveComponent(*actor->GetComponent<Rigidbody2D>());
    Steps(app, 1);
    CHECK(actor->GetRenderTransform().position.x == actor->transform.position.x);
}
