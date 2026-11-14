// Application のテスト。Run(ウィンドウと GPU)は使わず、StepFrame で1フレームずつ進める。
// dt は FrameClock::SetFixedDelta で固定するので、結果は毎回同じになる。
// FrameClock は dt を上限(既定 0.1 秒)で切り詰めるので、固定する dt はそれ以下にする。
#include "core/MemoryTracker.h"
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/SceneSerializer.h"
#include "engine/SpriteComponent.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::World;
using husky::platform::Key;
using husky::platform::KeyEvent;

namespace
{
    const std::vector<KeyEvent> NoEvents;

    // 毎フレーム、秒速 100 で右へ動く。Space が押されている間は、さらに秒速 100 で下へ動く。
    class Walker : public Actor
    {
        HUSKY_OBJECT_INLINE(Walker, Actor)
    public:
        void Tick(double dt) override
        {
            transform.position.x += static_cast<float>(100.0 * dt);
            const World* scene = GetWorld();
            if (scene != nullptr && scene->GetInput()->IsDown(Key::Space))
            {
                transform.position.y += static_cast<float>(100.0 * dt);
            }
        }
    };

    // Tick で "T" と書く。
    std::string g_log;
    class TickLogger : public Actor
    {
        HUSKY_OBJECT_INLINE(TickLogger, Actor)
    public:
        void Tick(double) override { g_log += "T"; }
    };
}

TEST_CASE("Application: シーンは最初からあり、GC の根で、入力が渡されている")
{
    Application app;
    CHECK(app.GetRegistry().GetObjectCount() == 1); // シーンだけ
    CHECK(app.GetRegistry().GetRootCount() == 1);
    CHECK(app.GetWorld().GetInput() == &app.GetInput());
}

TEST_CASE("Application: StepFrame は固定した dt でシーンを更新し、その dt を返す")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetWorld().Spawn<Walker>();

    CHECK(app.StepFrame(0, NoEvents) == doctest::Approx(0.1));
    CHECK(app.StepFrame(0, NoEvents) == doctest::Approx(0.1));
    CHECK(walker->transform.position.x == doctest::Approx(20.0)); // 100 × 0.1 × 2 フレーム
}

TEST_CASE("Application: StepFrame に渡したキーイベントが、アクターの読む入力になる")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetWorld().Spawn<Walker>();

    app.StepFrame(0, {KeyEvent{Key::Space, true}}); // Space を押す
    CHECK(app.GetInput().WasPressed(Key::Space));
    CHECK(walker->transform.position.y == doctest::Approx(10.0));

    app.StepFrame(0, NoEvents); // 押しっぱなし
    CHECK(walker->transform.position.y == doctest::Approx(20.0));

    app.StepFrame(0, {KeyEvent{Key::Space, false}}); // 離す
    CHECK(walker->transform.position.y == doctest::Approx(20.0));
}

TEST_CASE("Application: 更新コールバックは、シーンの更新の前に、同じ dt で呼ばれる")
{
    g_log.clear();
    Application app;
    app.GetClock().SetFixedDelta(0.05);
    app.GetWorld().Spawn<TickLogger>();
    double receivedDt = 0.0;
    app.SetUpdateCallback([&](Application&, double dt) {
        g_log += "U";
        receivedDt = dt;
    });

    app.StepFrame(0, NoEvents);

    CHECK(g_log == "UT");
    CHECK(receivedDt == doctest::Approx(0.05));
}

TEST_CASE("Application: Destroy したアクターは、そのフレームの終わりに消える")
{
    Application app;
    app.GetClock().SetFixedDelta(0.1);
    Walker* walker = app.GetWorld().Spawn<Walker>();
    const auto handle = walker->GetHandle();

    walker->Destroy();
    app.StepFrame(0, NoEvents);

    CHECK(app.GetRegistry().Resolve(handle) == nullptr);
    CHECK(app.GetWorld().actors.empty());
}

TEST_CASE("Application: GC は GcIntervalSeconds(1 秒)ごとに回り、シーンから届かないものを回収する")
{
    Application app;
    // 1/16 秒は2進数でちょうど表せるので、16 回足すとぴったり 1.0 になる。
    // (0.1 は2進数で表せず、10 回足しても 0.9999999... になって、GC が1フレーム遅れる)
    app.GetClock().SetFixedDelta(0.0625);
    app.GetRegistry().Create<Walker>(); // シーンにいない(根から届かない)アクター
    CHECK(app.GetRegistry().GetObjectCount() == 2);

    for (int i = 0; i < 15; ++i) // 0.9375 秒まで
    {
        app.StepFrame(0, NoEvents);
    }
    CHECK(app.GetRegistry().GetObjectCount() == 2); // まだ GC は回っていない

    app.StepFrame(0, NoEvents); // 1.0 秒 → GC
    CHECK(app.GetRegistry().GetObjectCount() == 1);
    CHECK(app.GetCollectedLastGc() == 1);
}

TEST_CASE("Application: 温まった後は、GC を回すフレームも含めて、StepFrame はメモリを確保しない")
{
    // 毎フレームの確保は、数が多いと遅くなり、断片化の原因にもなる。
    // 作業用の配列を使い回すなどして、普段のフレームでは確保しないようにしてある。それが崩れていないかを確かめる。
    Application app;
    for (int i = 0; i < 1000; ++i)
    {
        app.GetWorld().Spawn<Walker>()->AddComponent<husky::engine::SpriteComponent>();
    }
    app.GetClock().SetFixedDelta(0.0625); // 16 フレームで 1 秒 = GC 1 回
    for (int i = 0; i < 40; ++i)          // 温める: 作業用の配列が一番大きくなるまで回す(GC も 2 回回る)
    {
        app.StepFrame(0, NoEvents);
    }

    // CHECK も中で確保することがあるので、数える間は CHECK を挟まない。
    const std::uint64_t before = husky::core::GetMemoryStats().totalAllocations;
    for (int i = 0; i < 32; ++i) // 2 秒分(GC が 2 回回る)
    {
        app.StepFrame(0, NoEvents);
    }
    const std::uint64_t after = husky::core::GetMemoryStats().totalAllocations;

    CHECK(after - before == 0);
}

TEST_CASE("Application: engine の型を、型の登録簿に登録済み")
{
    Application app;
    husky::core::TypeRegistry& types = app.GetTypeRegistry();
    CHECK(types.Find("World") != nullptr);
    CHECK(types.Find("Actor") != nullptr);
    CHECK(types.Find("Component") != nullptr);
    CHECK(types.Find("SpriteComponent") != nullptr);

    husky::core::Object* sprite = types.Create("SpriteComponent", app.GetRegistry());
    CHECK(husky::core::Cast<husky::engine::SpriteComponent>(sprite) != nullptr);
}

TEST_CASE("Application: LoadSceneFile は、保存したシーンのファイルを読み込み、今のシーンの後ろに足す")
{
    // 保存する側: アクター1つ(色付きのスプライト付き)をファイルに書く。
    const std::string path = husky::platform::GetExecutableDirectory() + "husky_test_scene.json";
    {
        Application source;
        Walker* walker = source.GetWorld().Spawn<Walker>();
        walker->name = "ファイルから";
        walker->AddComponent<husky::engine::SpriteComponent>()->color = {0.25f, 0.5f, 0.75f, 1.0f};
        source.GetTypeRegistry().Register<Walker>();

        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(husky::engine::SceneSerializer::Write(source.GetWorld()).value, text));
        REQUIRE(husky::platform::WriteTextFile(path.c_str(), text));
    }

    // 読み込む側: 型を登録してから読み込む。
    Application app;
    app.GetTypeRegistry().Register<Walker>();
    app.GetWorld().Spawn<Walker>(); // 先にいるアクターは消えない
    REQUIRE(app.LoadSceneFile(path));

    REQUIRE(app.GetWorld().actors.size() == 2);
    Walker* loaded = app.GetRegistry().Resolve<Walker>(app.GetWorld().actors[1]);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->name == "ファイルから");
    CHECK(loaded->GetComponent<husky::engine::SpriteComponent>()->color.b == 0.75f);

    // 読み込んだアクターも、普通に更新される
    app.GetClock().SetFixedDelta(0.1);
    app.StepFrame(0, NoEvents);
    CHECK(loaded->transform.position.x == doctest::Approx(10.0f));

    husky::platform::RemoveFile(path.c_str());
}

TEST_CASE("Application: LoadSceneFile は、無いファイルや壊れた JSON では false を返し、シーンを変えない")
{
    Application app;
    CHECK_FALSE(app.LoadSceneFile(husky::platform::GetExecutableDirectory() + "husky_no_such_scene.json"));

    const std::string path = husky::platform::GetExecutableDirectory() + "husky_test_broken_scene.json";
    REQUIRE(husky::platform::WriteTextFile(path.c_str(), "{ \"objects\": [ "));
    CHECK_FALSE(app.LoadSceneFile(path));
    CHECK(app.GetWorld().actors.empty());
    husky::platform::RemoveFile(path.c_str());
}

TEST_CASE("Application: 開発ビルドの印(HUSKY_DEVELOPMENT)は Debug だけに付き、製品(Release)には付かない")
{
    // NDEBUG は、Release の構成で CMake が付ける標準の印(assert を消すためのもの)。
#if defined(NDEBUG)
#if defined(HUSKY_DEVELOPMENT)
    FAIL("Release なのに HUSKY_DEVELOPMENT が付いている");
#endif
#else
#if !defined(HUSKY_DEVELOPMENT)
    FAIL("Debug なのに HUSKY_DEVELOPMENT が付いていない");
#endif
#endif
    CHECK(true);
}

#if HUSKY_DEVELOPMENT
TEST_CASE("Application(開発ビルド): SaveSceneFile で保存したものを、LoadSceneFile で読み戻せる")
{
    const std::string path = husky::platform::GetExecutableDirectory() + "husky_test_saved_scene.json";
    {
        Application source;
        Walker* walker = source.GetWorld().Spawn<Walker>();
        walker->name = "保存した";
        walker->transform.position.y = 42.0f;
        REQUIRE(source.SaveSceneFile(path));
    }

    std::string text;
    REQUIRE(husky::platform::ReadTextFile(path.c_str(), text));
    CHECK(text.find("\n  ") != std::string::npos); // 読みやすい形(字下げあり)で書かれている

    Application app;
    app.GetTypeRegistry().Register<Walker>();
    REQUIRE(app.LoadSceneFile(path));
    REQUIRE(app.GetWorld().actors.size() == 1);
    Walker* loaded = app.GetRegistry().Resolve<Walker>(app.GetWorld().actors[0]);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->name == "保存した");
    CHECK(loaded->transform.position.y == 42.0f);

    husky::platform::RemoveFile(path.c_str());
}
#endif
