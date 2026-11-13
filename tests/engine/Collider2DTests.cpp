// コライダーのコンポーネントと、重なりのイベントのテスト(COL-3)。
// Application::StepFrame(シーンの更新 → 当たり判定の同期 → 重なりの調べ → イベント)を、決まった dt で回して確かめる。
#include "engine/Collider2D.h"

#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h"

#include <doctest/doctest.h>

#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::BoxCollider2D;
using husky::engine::CircleCollider2D;
using husky::engine::OverlapEvent;

namespace
{
    // 届いたイベントを覚えるアクター。
    class OverlapRecorder : public Actor
    {
        HUSKY_OBJECT(OverlapRecorder, Actor)
    public:
        void OnOverlap(const OverlapEvent& event) override { events.push_back(event); }
        std::vector<OverlapEvent> events;
    };

    // 届いたイベントの数を数えるコンポーネント(アクターだけでなく、コンポーネントにも届くことを確かめる)。
    class OverlapCounter : public husky::engine::Component
    {
        HUSKY_OBJECT(OverlapCounter, husky::engine::Component)
    public:
        void OnOverlap(const OverlapEvent& event) override { event.begin ? ++begins : ++ends; }
        int begins = 0;
        int ends = 0;
    };

    OverlapRecorder* SpawnBox(Application& app, float x, float y)
    {
        OverlapRecorder* actor = app.GetScene().Spawn<OverlapRecorder>();
        actor->transform.position = {x, y, 0.0f};
        actor->AddComponent<BoxCollider2D>(); // 32 x 32
        return actor;
    }

    void Step(Application& app)
    {
        app.StepFrame(0, {});
    }
}

TEST_CASE("COL-3: 重なり始めると両方のアクターに begin、離れると end が1回ずつ届く")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* a = SpawnBox(app, 0.0f, 0.0f);
    OverlapRecorder* b = SpawnBox(app, 100.0f, 0.0f);
    Step(app);
    CHECK(app.GetPhysics2D().GetColliderCount() == 2);
    CHECK(a->events.empty());

    // B を A に重ねる(x の差 20 < 32)
    b->transform.position.x = 20.0f;
    Step(app);
    REQUIRE(a->events.size() == 1);
    CHECK(a->events[0].begin);
    CHECK(a->events[0].otherActor == b->GetHandle());
    REQUIRE(b->events.size() == 1);
    CHECK(b->events[0].otherActor == a->GetHandle());
    CHECK(b->events[0].selfCollider == b->GetComponent<BoxCollider2D>()->GetHandle());

    // 重なったままなら、もう届かない
    Step(app);
    CHECK(a->events.size() == 1);

    // 離すと end
    b->transform.position.x = 100.0f;
    Step(app);
    REQUIRE(a->events.size() == 2);
    CHECK_FALSE(a->events[1].begin);
    CHECK(a->events[1].otherActor == b->GetHandle());
}

TEST_CASE("COL-3: 重なっている相手が消えると end が届き、世界からも外れる")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* a = SpawnBox(app, 0.0f, 0.0f);
    OverlapRecorder* b = SpawnBox(app, 10.0f, 0.0f);
    const husky::core::ObjectHandle bHandle = b->GetHandle();
    Step(app);
    REQUIRE(a->events.size() == 1);
    b->Destroy();
    Step(app); // このフレームの終わりで B が消える
    Step(app); // 次のフレームの同期で、B のコライダーが世界から外れ、A に end
    CHECK(app.GetPhysics2D().GetColliderCount() == 1);
    REQUIRE(a->events.size() == 2);
    CHECK_FALSE(a->events[1].begin);
    CHECK(a->events[1].otherActor == bHandle); // 消えた相手も、ハンドルで分かる(もう引けないが、どれだったかは分かる)
}

TEST_CASE("COL-3: レイヤーとマスク: お互いに相手のグループを選んでいるときだけ届く。同じアクターのコライダーどうしは数えない")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* a = SpawnBox(app, 0.0f, 0.0f);
    OverlapRecorder* b = SpawnBox(app, 10.0f, 0.0f);
    // A はグループ 1、B はグループ 0。A はグループ 0 とだけ当たるが、B はグループ 0 とだけ当たる(A を選んでいない)
    a->GetComponent<BoxCollider2D>()->layer = 1;
    a->GetComponent<BoxCollider2D>()->collisionMask = 1 << 0;
    b->GetComponent<BoxCollider2D>()->collisionMask = 1 << 0;
    // A にもう1つ円を付ける(同じアクターのコライダーどうしは重なっていても数えない)
    a->AddComponent<CircleCollider2D>()->layer = 1;
    a->GetComponent<CircleCollider2D>()->collisionMask = 1 << 0;
    Step(app);
    CHECK(a->events.empty());
    CHECK(b->events.empty());

    // B が A のグループ(1)も選ぶと、A の四角と円の両方との重なりが届く
    b->GetComponent<BoxCollider2D>()->collisionMask = (1 << 0) | (1 << 1);
    Step(app);
    CHECK(a->events.size() == 2); // A の四角から見た B、A の円から見た B
    CHECK(b->events.size() == 2); // B から見た A の四角と円
}

TEST_CASE("COL-3: イベントはアクターのコンポーネントにも届く。編集中(シーンを止めている間)は届かない")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* a = SpawnBox(app, 0.0f, 0.0f);
    OverlapCounter* counter = a->AddComponent<OverlapCounter>();
    OverlapRecorder* b = SpawnBox(app, 100.0f, 0.0f);
    Step(app);

    app.SetSceneTickEnabled(false); // 編集中と同じ
    b->transform.position.x = 10.0f;
    Step(app);
    CHECK(a->events.empty());
    CHECK(counter->begins == 0);

    app.SetSceneTickEnabled(true);
    Step(app);
    CHECK(a->events.size() == 1);
    CHECK(counter->begins == 1);
}

TEST_CASE("COL-3: 大きさを変えると置き直され、回転と拡大縮小にも合わせて重なりが決まる")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* a = SpawnBox(app, 0.0f, 0.0f);
    OverlapRecorder* b = SpawnBox(app, 50.0f, 0.0f); // 32 x 32 どうし、真ん中が 50 離れている → 重ならない
    Step(app);
    CHECK(a->events.empty());
    // A の幅を 80 にすると(右の辺が 40)、B の左の辺(34)に重なる
    a->GetComponent<BoxCollider2D>()->size = {80.0f, 32.0f};
    Step(app);
    CHECK(app.GetPhysics2D().GetColliderCount() == 2);
    REQUIRE(a->events.size() == 1);
    // A を 90 度回すと、横に伸びていた分が縦になり、離れる
    a->transform.rotation = 90.0f;
    Step(app);
    REQUIRE(a->events.size() == 2);
    CHECK_FALSE(a->events[1].begin);
    // A の四角を元の大きさに戻し、x を 3 倍に拡大する(幅 96 → 右の辺が 48)。B の四角の左の辺(34)に届く
    a->transform.rotation = 0.0f;
    a->GetComponent<BoxCollider2D>()->size = {32.0f, 32.0f};
    a->transform.scale = {3.0f, 1.0f, 1.0f};
    Step(app);
    CHECK(a->events.size() == 3);
    REQUIRE_FALSE(b->events.empty());
    CHECK(b->events.back().begin); // B の側にも、また重なり始めたことが届く
}

TEST_CASE("COL-3: エディタの枠の形(ComputeBoxColliderCorners / ComputeCircleColliderWorld)は、当たり判定の形と一致する")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* actor = app.GetScene().Spawn<OverlapRecorder>();
    actor->transform.position = {100.0f, 50.0f, 0.0f};
    actor->transform.rotation = 30.0f;
    actor->transform.scale = {2.0f, 1.0f, 1.0f};
    BoxCollider2D* box = actor->AddComponent<BoxCollider2D>();
    box->size = {40.0f, 20.0f};
    box->offset = {10.0f, 5.0f};
    Step(app);

    // 四隅のそれぞれから、四角の真ん中へ少し入った点は当たり、外へ少し出た点は当たらない
    const husky::render::SpriteCorners corners = husky::engine::ComputeBoxColliderCorners(actor->transform, *box);
    const float centerX = (corners.x[0] + corners.x[2]) * 0.5f;
    const float centerY = (corners.y[0] + corners.y[2]) * 0.5f;
    std::vector<husky::engine::QueryHit2D> hits;
    for (int i = 0; i < 4; ++i)
    {
        const float inX = corners.x[i] + (centerX - corners.x[i]) * 0.05f;
        const float inY = corners.y[i] + (centerY - corners.y[i]) * 0.05f;
        app.GetPhysics2D().GetWorld().OverlapPoint(inX, inY, husky::engine::AllCollisionLayers, hits);
        CHECK(hits.size() == 1);
        const float outX = corners.x[i] - (centerX - corners.x[i]) * 0.05f;
        const float outY = corners.y[i] - (centerY - corners.y[i]) * 0.05f;
        app.GetPhysics2D().GetWorld().OverlapPoint(outX, outY, husky::engine::AllCollisionLayers, hits);
        CHECK(hits.empty());
    }

    // 円: offset (10, 0) を 30 度回し 2 倍にした所が真ん中、半径は 2 倍
    CircleCollider2D circle;
    circle.radius = 10.0f;
    circle.offset = {10.0f, 0.0f};
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    husky::engine::ComputeCircleColliderWorld(actor->transform, circle, x, y, radius);
    CHECK(radius == doctest::Approx(20.0f));
    CHECK(x == doctest::Approx(100.0f + 20.0f * 0.8660254f));
    CHECK(y == doctest::Approx(50.0f + 20.0f * 0.5f));
}

TEST_CASE("宿題: 左右反転(拡大 x がマイナス)でも、エディタの枠と当たり判定の形が一致する")
{
    Application app;
    app.GetClock().SetFixedDelta(1.0 / 60.0);
    OverlapRecorder* actor = app.GetScene().Spawn<OverlapRecorder>();
    actor->transform.position = {100.0f, 0.0f, 0.0f};
    actor->transform.scale = {-1.0f, 1.0f, 1.0f};
    BoxCollider2D* box = actor->AddComponent<BoxCollider2D>();
    box->size = {20.0f, 20.0f};
    box->offset = {30.0f, 0.0f}; // 反転すると、持ち主の左 30 の所
    Step(app);
    const husky::render::SpriteCorners corners = husky::engine::ComputeBoxColliderCorners(actor->transform, *box);
    const float centerX = (corners.x[0] + corners.x[2]) * 0.5f;
    CHECK(centerX == doctest::Approx(70.0f));
    std::vector<husky::engine::QueryHit2D> hits;
    app.GetPhysics2D().GetWorld().OverlapPoint(70.0f, 0.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.size() == 1);
    app.GetPhysics2D().GetWorld().OverlapPoint(130.0f, 0.0f, husky::engine::AllCollisionLayers, hits);
    CHECK(hits.empty());
}

TEST_CASE("使い勝手: FitColliderToSprite はコライダーを絵の大きさと位置に合わせる(四角はそのまま、円は大きいほうの辺)")
{
    Application app;
    Actor* actor = app.GetScene().Spawn<Actor>();
    husky::engine::SpriteComponent* sprite = actor->AddComponent<husky::engine::SpriteComponent>();
    sprite->width = 128.0f;
    sprite->height = 64.0f;
    BoxCollider2D* box = actor->AddComponent<BoxCollider2D>();
    REQUIRE(husky::engine::FitColliderToSprite(*box, *actor));
    CHECK(box->size.x == 128.0f);
    CHECK(box->size.y == 64.0f);
    CHECK(box->offset.x == 0.0f); // ピボットが真ん中なら、ずらしは 0
    // ピボットが左下なら、絵の真ん中は (64, 32) にある
    sprite->pivot = {0.0f, 0.0f};
    CircleCollider2D* circle = actor->AddComponent<CircleCollider2D>();
    REQUIRE(husky::engine::FitColliderToSprite(*circle, *actor));
    CHECK(circle->radius == 64.0f);
    CHECK(circle->offset.x == 64.0f);
    CHECK(circle->offset.y == 32.0f);
    // スプライトが無ければ何もしない
    Actor* bare = app.GetScene().Spawn<Actor>();
    BoxCollider2D* untouched = bare->AddComponent<BoxCollider2D>();
    CHECK_FALSE(husky::engine::FitColliderToSprite(*untouched, *bare));
    CHECK(untouched->size.x == 32.0f);
}
