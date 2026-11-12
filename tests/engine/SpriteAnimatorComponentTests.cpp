// SpriteAnimatorComponent(スプライトのアニメーションの再生)のテスト。GPU は使わない(どの切り出しを描くかだけを確かめる)。
// dt はテストから渡すので、結果は毎回同じになる。
#include "engine/SpriteAnimatorComponent.h"

#include "engine/Application.h"
#include "engine/ObjectSerializer.h"
#include "engine/SpriteComponent.h"
#include "TestFolder.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::SpriteAnimatorComponent;
using husky::engine::SpriteComponent;

namespace
{
    // 切り出し 5, 6, 7 と、アニメーション walk(5→6→7 を繰り返す)と once(5→6 で止まる)。1 コマは 0.25 秒。
    // 0.25 は 2 進数でちょうど表せるので、時間を足し合わせても誤差が出ず、コマの境目を確実に確かめられる。
    constexpr std::int32_t Walk = 20;
    constexpr std::int32_t Once = 21;

    void WriteSheetAssets(const husky::tests::TestFolder& folder)
    {
        std::string png;
        REQUIRE(husky::platform::ReadTextFile(
            (husky::platform::GetExecutableDirectory() + "assets/textures/test_sprite.png").c_str(), png));
        folder.Write("sheet.png", png);
        folder.Write("sheet.png.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a51" })");
        folder.Write("sheet.spritesheet", R"({ "texture": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a51", "sprites": [
            { "id": 5, "name": "a", "x": 0,  "y": 0, "width": 8, "height": 8 },
            { "id": 6, "name": "b", "x": 8,  "y": 0, "width": 8, "height": 8 },
            { "id": 7, "name": "c", "x": 16, "y": 0, "width": 8, "height": 8 }
        ], "animations": [
            { "id": 20, "name": "walk", "frames": [5, 6, 7], "frameSeconds": 0.25, "loop": true },
            { "id": 21, "name": "once", "frames": [5, 6], "frameSeconds": 0.25, "loop": false }
        ] })");
        folder.Write("sheet.spritesheet.meta", R"({ "guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a52" })");
    }

    husky::core::Guid SheetGuid() { return husky::tests::MakeGuid(0x3f2a9c1e7b404d2eull, 0x9a1f0c5e8b7d6a52ull); }

    // スプライトシートを指す SpriteComponent と、アニメーターを持つアクターを置く。
    struct AnimatedActor
    {
        SpriteComponent* sprite = nullptr;
        SpriteAnimatorComponent* animator = nullptr;
    };

    AnimatedActor SpawnAnimatedActor(Application& app)
    {
        Actor* actor = app.GetScene().Spawn<Actor>();
        REQUIRE(actor != nullptr);
        AnimatedActor result;
        result.sprite = actor->AddComponent<SpriteComponent>();
        result.animator = actor->AddComponent<SpriteAnimatorComponent>();
        REQUIRE(result.sprite != nullptr);
        REQUIRE(result.animator != nullptr);
        result.sprite->sheet.guid = SheetGuid();
        return result;
    }

    // ゲームでは描画(DrawSprites)がスプライトシートを読み始める。テストでは GPU を使わないので、ここで読んで待つ。
    void LoadSheet(Application& app, SpriteComponent& sprite)
    {
        app.GetAssetLoader().Resolve(sprite.sheet);
        app.GetAssetLoader().WaitForAllLoads();
    }
}

TEST_CASE("SpriteAnimator: 時間が進むと、SpriteComponent の切り出しの ID がコマの順に変わり、最後の後は最初に戻る")
{
    husky::tests::TestFolder folder("husky_test_animator_loop");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    LoadSheet(app, *actor.sprite);

    actor.animator->Play(Walk);
    actor.animator->Tick(0.0);
    CHECK(actor.sprite->sprite == 5); // 0 秒: 最初のコマ
    actor.animator->Tick(0.125);
    CHECK(actor.sprite->sprite == 5); // 0.125 秒: まだ最初のコマ
    actor.animator->Tick(0.125);
    CHECK(actor.sprite->sprite == 6); // 0.25 秒: 2 コマ目
    actor.animator->Tick(0.25);
    CHECK(actor.sprite->sprite == 7);
    actor.animator->Tick(0.25);
    CHECK(actor.sprite->sprite == 5); // 0.75 秒: 繰り返して最初に戻る
    CHECK(actor.animator->GetTime() == doctest::Approx(0.75));
}

TEST_CASE("SpriteAnimator: 繰り返さないアニメーションは、最後のコマで止まる")
{
    husky::tests::TestFolder folder("husky_test_animator_once");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    LoadSheet(app, *actor.sprite);

    actor.animator->Play(Once);
    actor.animator->Tick(0.25);
    CHECK(actor.sprite->sprite == 6);
    actor.animator->Tick(10.0);
    CHECK(actor.sprite->sprite == 6);
}

TEST_CASE("SpriteAnimator: 速さの倍率を掛ける。止めている間と、倍率が 0 以下の間は、時間が進まない")
{
    husky::tests::TestFolder folder("husky_test_animator_speed");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    LoadSheet(app, *actor.sprite);

    actor.animator->Play(Walk);
    actor.animator->speed = 2.0f;
    actor.animator->Tick(0.125); // 2 倍なので 0.25 秒ぶん進む
    CHECK(actor.sprite->sprite == 6);
    CHECK(actor.animator->GetTime() == doctest::Approx(0.25));

    actor.animator->playing = false;
    actor.animator->Tick(1.0);
    CHECK(actor.animator->GetTime() == doctest::Approx(0.25));
    CHECK(actor.sprite->sprite == 6); // 今のコマのまま

    actor.animator->playing = true;
    actor.animator->speed = 0.0f;
    actor.animator->Tick(1.0);
    actor.animator->speed = -1.0f; // 逆再生はしない
    actor.animator->Tick(1.0);
    CHECK(actor.animator->GetTime() == doctest::Approx(0.25));
}

TEST_CASE("SpriteAnimator: アニメーションの ID を書き換えると、新しいアニメーションの最初のコマから始まる")
{
    husky::tests::TestFolder folder("husky_test_animator_switch");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    LoadSheet(app, *actor.sprite);

    actor.animator->Play(Walk);
    actor.animator->Tick(0.5);
    CHECK(actor.sprite->sprite == 7);

    actor.animator->animation = Once; // インスペクタやファイルの読み込みと同じく、プロパティを直接書き換える
    actor.animator->Tick(0.0);
    CHECK(actor.animator->GetTime() == 0.0);
    CHECK(actor.sprite->sprite == 5);

    actor.animator->Play(Once); // 同じ ID でも、Play は最初からやり直す
    CHECK(actor.animator->GetTime() == 0.0);
}

TEST_CASE("SpriteAnimator: アニメーションが見つからない、スプライトシートが読めていない、見た目が無いときは何もしない")
{
    husky::tests::TestFolder folder("husky_test_animator_missing");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    actor.sprite->sprite = 7;

    // スプライトシートを読む前: 時間も進めない(見え始めたときに、最初のコマから再生するため)
    actor.animator->Play(Walk);
    actor.animator->Tick(0.5);
    CHECK(actor.animator->GetTime() == 0.0);
    CHECK(actor.sprite->sprite == 7);

    LoadSheet(app, *actor.sprite);
    actor.animator->Play(999); // スプライトシートに無い ID
    actor.animator->Tick(0.5);
    CHECK(actor.sprite->sprite == 7);
    actor.animator->Play(0); // 何も指さない
    actor.animator->Tick(0.5);
    CHECK(actor.sprite->sprite == 7);

    // SpriteComponent の無いアクターに付けても、落ちない
    Actor* alone = app.GetScene().Spawn<Actor>();
    SpriteAnimatorComponent* lonely = alone->AddComponent<SpriteAnimatorComponent>();
    lonely->Play(Walk);
    lonely->Tick(0.5);
    CHECK(lonely->GetTime() == 0.0);
}

TEST_CASE("SpriteAnimator: シーンの更新(StepFrame)から、毎フレーム呼ばれる。描く直前の順番(PreRender)")
{
    husky::tests::TestFolder folder("husky_test_animator_step");
    WriteSheetAssets(folder);
    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AnimatedActor actor = SpawnAnimatedActor(app);
    LoadSheet(app, *actor.sprite);
    actor.animator->Play(Walk);
    CHECK(actor.animator->GetTickOrder() == husky::engine::TickOrder::PreRender);

    app.GetClock().SetFixedDelta(0.0625); // 1/16 秒(2 進数でちょうど表せる)
    for (int frame = 0; frame < 4; ++frame)
    {
        app.StepFrame(0, {});
    }
    CHECK(actor.animator->GetTime() == doctest::Approx(0.25));
    CHECK(actor.sprite->sprite == 6);
}

TEST_CASE("SpriteAnimator: アニメーションの ID、再生中か、速さを保存して読み戻せる。経った時間は保存しない")
{
    namespace ObjectSerializer = husky::engine::ObjectSerializer;

    husky::core::ObjectRegistry registry;
    auto* animator = registry.Create<SpriteAnimatorComponent>();
    animator->animation = Walk;
    animator->playing = false;
    animator->speed = 1.5f;
    const husky::engine::ArchiveValue saved = ObjectSerializer::Write(*animator);
    const husky::engine::ArchiveValue* properties = saved.Find("properties");
    REQUIRE(properties != nullptr);
    CHECK(properties->Find("animation") != nullptr);
    CHECK(properties->Find("playing") != nullptr);
    CHECK(properties->Find("speed") != nullptr);
    CHECK(properties->Find("time") == nullptr);

    auto* copy = registry.Create<SpriteAnimatorComponent>();
    CHECK(ObjectSerializer::ReadProperties(saved, *copy).warnings.empty());
    CHECK(copy->animation == Walk);
    CHECK_FALSE(copy->playing);
    CHECK(copy->speed == 1.5f);
    CHECK(copy->GetTime() == 0.0);
}

TEST_CASE("SpriteAnimator: 型の名前から作れる(RegisterEngineTypes に登録されている)")
{
    Application app;
    CHECK(app.GetTypeRegistry().Find("SpriteAnimatorComponent") != nullptr);
}
