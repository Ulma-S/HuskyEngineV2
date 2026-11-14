// 入れ物のシーン(SC-2b): ルートシーン、シーンの一覧(名前 → GUID)、LoadScene / UnloadScene、入れ子、親子はシーンをまたげない。
#include "TestFolder.h"
#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::core::Guid;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::Scene;
using husky::engine::World;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;

namespace
{
    // 名前を付けたアクターのシーンのファイルの文字列。childOfFirst が true なら、2体目を1体目の子にする。
    std::string MakeSceneText(const std::vector<std::string>& names, bool childOfFirst = false)
    {
        Application source;
        std::vector<Actor*> spawned;
        for (const std::string& actorName : names)
        {
            Actor* actor = source.GetWorld().Spawn<Actor>();
            actor->name = actorName;
            spawned.push_back(actor);
        }
        if (childOfFirst && spawned.size() >= 2)
        {
            REQUIRE(spawned[1]->SetParent(spawned[0]));
        }
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(husky::engine::SceneSerializer::Write(source.GetWorld()).value, text, true));
        return text;
    }

    // .scene と .meta を書く。
    void WriteScene(TestFolder& folder, const std::string& path, const Guid& guid, const std::string& text)
    {
        folder.Write(path, text);
        folder.Write(path + ".meta", "{\"guid\": \"" + guid.ToString() + "\"}");
    }

    // シーンの一覧(.sdata)を書く。
    void WriteSceneList(TestFolder& folder, const Guid& guid, const std::vector<Guid>& scenes)
    {
        std::string list;
        for (const Guid& scene : scenes)
        {
            list += (list.empty() ? "\"" : ", \"") + scene.ToString() + "\"";
        }
        folder.Write("settings/scenes.sdata", "{\"type\": \"SceneList\", \"properties\": {\"scenes\": [" + list + "]}}");
        folder.Write("settings/scenes.sdata.meta", "{\"guid\": \"" + guid.ToString() + "\"}");
    }

    // ワールドの生きているアクターから、名前で探す(一覧の最初のもの)。
    Actor* FindActor(Application& app, const std::string& actorName)
    {
        Actor* found = nullptr;
        app.GetWorld().ForEachActor([&](Actor& actor) {
            if (found == nullptr && actor.name == actorName)
            {
                found = &actor;
            }
        });
        return found;
    }

    // title(Logo と、その子の Sub)、main(Player)、result(Score)の3つのシーンと一覧を置く。
    struct ThreeScenes
    {
        TestFolder folder{"husky_test_scene_tree"};
        ThreeScenes()
        {
            WriteScene(folder, "scenes/title.scene", MakeGuid(11, 1), MakeSceneText({"Logo", "Sub"}, true));
            WriteScene(folder, "scenes/main.scene", MakeGuid(11, 2), MakeSceneText({"Player"}));
            WriteScene(folder, "scenes/result.scene", MakeGuid(11, 3), MakeSceneText({"Score"}));
            WriteSceneList(folder, MakeGuid(11, 9), {MakeGuid(11, 1), MakeGuid(11, 2), MakeGuid(11, 3)});
        }
    };

    void Prepare(Application& app, TestFolder& folder)
    {
        REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
        REQUIRE(app.LoadSceneList(MakeGuid(11, 9)));
    }
}

TEST_CASE("Scene: ルートシーンは1つで、置き場所を指定しないアクターはルートシーンに入る(SC-2b)")
{
    Application app;
    World& world = app.GetWorld();
    Scene* root = world.GetRootScene();
    REQUIRE(root != nullptr);
    CHECK(root->IsRoot());
    CHECK(world.GetRootScene() == root); // いつも同じもの

    Actor* actor = world.Spawn<Actor>();
    CHECK(actor->GetScene() == root);
    CHECK(app.GetRegistry().Create<Actor>()->GetScene() == nullptr); // ワールドに入れていないアクター

    // GC で回収されない(ワールド → ルートシーン)
    app.GetRegistry().CollectGarbage();
    CHECK(world.GetRootScene() == root);
}

TEST_CASE("Scene: 一覧の名前で LoadScene すると、フレームの物理の後に子のシーンとして読み、UnloadScene で中身ごと消える(SC-2b)")
{
    ThreeScenes files;
    Application app;
    Prepare(app, files.folder);
    World& world = app.GetWorld();
    CHECK(app.GetFirstSceneName() == "title");
    REQUIRE(app.FindSceneGuid("main") != nullptr);
    CHECK(*app.FindSceneGuid("main") == MakeGuid(11, 2));
    CHECK(app.FindSceneGuid("nothing") == nullptr);

    // 頼んだだけではまだ読まない
    world.LoadScene("title");
    CHECK(world.FindScene("title") == nullptr);
    app.StepFrame(16'000'000, {}); // このフレームの中で読む
    Scene* title = world.FindScene("title");
    REQUIRE(title != nullptr);
    CHECK(title->GetParent() == world.GetRootScene());
    CHECK(title->asset == MakeGuid(11, 1));
    Actor* logo = FindActor(app, "Logo");
    Actor* sub = FindActor(app, "Sub");
    REQUIRE(logo != nullptr);
    REQUIRE(sub != nullptr);
    CHECK(logo->GetScene() == title);
    CHECK(sub->GetScene() == title); // 子も同じシーン
    CHECK(sub->GetParent() == logo);

    // 下ろすと、中のアクターはこのフレームの終わりに消え、シーンも木から外れる
    world.UnloadScene("title");
    app.StepFrame(32'000'000, {});
    CHECK(world.FindScene("title") == nullptr);
    CHECK(FindActor(app, "Logo") == nullptr);
    CHECK(FindActor(app, "Sub") == nullptr);
    CHECK(world.GetRootScene()->children.empty());

    // 下ろした後はまた読める(同じシーンは同時に1つだけ、という決まり)
    world.LoadScene("title");
    app.ProcessSceneRequests();
    CHECK(world.FindScene("title") != nullptr);
}

TEST_CASE("Scene: 入れ替えと入れ子。親のシーンを下ろすと、子のシーンとその中身も消える(SC-2b)")
{
    ThreeScenes files;
    Application app;
    Prepare(app, files.folder);
    World& world = app.GetWorld();
    Actor* keeper = world.Spawn<Actor>(); // ルートシーンのもの(BGM やスコアを持つ係のつもり)
    keeper->name = "Keeper";

    // 同じフレームに「title を下ろして main を読む」(タイトル → メイン)
    world.LoadScene("title");
    app.ProcessSceneRequests();
    world.UnloadScene("title");
    world.LoadScene("main");
    app.StepFrame(16'000'000, {});
    CHECK(world.FindScene("title") == nullptr);
    Scene* main = world.FindScene("main");
    REQUIRE(main != nullptr);
    CHECK(FindActor(app, "Player") != nullptr);
    CHECK(FindActor(app, "Keeper") == keeper); // ルートシーンのものは残る

    // main の下に result を入れ子で読む → main を下ろすと result も消える
    world.LoadScene("result", main);
    app.ProcessSceneRequests();
    Scene* result = world.FindScene("result");
    REQUIRE(result != nullptr);
    CHECK(result->GetParent() == main);
    CHECK(result->IsSelfOrDescendantOf(*main));
    world.UnloadScene("main");
    app.StepFrame(32'000'000, {});
    CHECK(world.FindScene("main") == nullptr);
    CHECK(world.FindScene("result") == nullptr);
    CHECK(FindActor(app, "Player") == nullptr);
    CHECK(FindActor(app, "Score") == nullptr);
    CHECK(FindActor(app, "Keeper") == keeper);
}

TEST_CASE("Scene: 断るもの: 一覧に無い名前、もう読んであるシーン、読んでいないシーン、ルートシーンを下ろす(SC-2b)")
{
    ThreeScenes files;
    Application app;
    Prepare(app, files.folder);
    World& world = app.GetWorld();

    world.LoadScene("nothing");
    world.LoadScene("main");
    world.LoadScene("main"); // 同じフレームに2回: 2回目は「もう読んである」
    world.UnloadScene("result");
    app.ProcessSceneRequests();
    CHECK(world.FindScene("nothing") == nullptr);
    CHECK(world.GetRootScene()->children.size() == 1);
    int players = 0;
    world.ForEachActor([&](Actor& actor) { players += actor.name == "Player" ? 1 : 0; });
    CHECK(players == 1);

    // ルートシーンは下ろせない(名前を付けて試す)
    world.GetRootScene()->name = "game";
    world.UnloadScene("game");
    app.ProcessSceneRequests();
    CHECK(world.FindScene("game") == world.GetRootScene());
    CHECK(world.GetSceneRequests().empty());
}

TEST_CASE("Scene: 親子はシーンをまたげない。MoveToScene は子孫ごと移す(SC-2b)")
{
    ThreeScenes files;
    Application app;
    Prepare(app, files.folder);
    World& world = app.GetWorld();
    world.LoadScene("main");
    app.ProcessSceneRequests();
    Scene* main = world.FindScene("main");
    REQUIRE(main != nullptr);
    Actor* player = FindActor(app, "Player");
    REQUIRE(player != nullptr);

    // ルートシーンのアクターを main のアクターの子にはできない(エラーのログ)
    Actor* rootActor = world.Spawn<Actor>();
    const char* reason = nullptr;
    CHECK_FALSE(rootActor->SetParent(player, true, &reason));
    CHECK(reason != nullptr);
    CHECK(rootActor->WhyCannotSetParent(player) != nullptr);
    CHECK(rootActor->GetParent() == nullptr);

    // 子孫ごと main へ移せば、親にできる
    Actor* child = world.Spawn<Actor>();
    REQUIRE(child->SetParent(rootActor));
    CHECK(world.MoveToScene(*rootActor, *main));
    CHECK(rootActor->GetScene() == main);
    CHECK(child->GetScene() == main);
    CHECK(rootActor->SetParent(player));

    // 親が別のシーンにいるアクターは、それだけを移せない
    CHECK_FALSE(world.MoveToScene(*child, *world.GetRootScene()));
    CHECK(child->GetScene() == main);

    // SpawnInScene は最初からそのシーンに置く
    Actor* placed = world.SpawnInScene<Actor>(*main);
    REQUIRE(placed != nullptr);
    CHECK(placed->GetScene() == main);
}

TEST_CASE("Scene: シーンの一覧で名前が重なれば後のものは使わない。UnloadAllChildScenes は木を空にする(SC-2b)")
{
    TestFolder folder("husky_test_scene_list_duplicate");
    WriteScene(folder, "a/title.scene", MakeGuid(12, 1), MakeSceneText({"A"}));
    WriteScene(folder, "b/title.scene", MakeGuid(12, 2), MakeSceneText({"B"}));
    folder.Write("settings/scenes.sdata", "{\"type\": \"SceneList\", \"properties\": {\"scenes\": [\"" + MakeGuid(12, 1).ToString()
                                              + "\", \"" + MakeGuid(12, 2).ToString() + "\", null]}}");
    folder.Write("settings/scenes.sdata.meta", "{\"guid\": \"" + MakeGuid(12, 9).ToString() + "\"}");

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    REQUIRE(app.LoadSceneList(MakeGuid(12, 9)));
    REQUIRE(app.FindSceneGuid("title") != nullptr);
    CHECK(*app.FindSceneGuid("title") == MakeGuid(12, 1)); // 先のもの

    World& world = app.GetWorld();
    world.LoadScene("title");
    app.ProcessSceneRequests();
    REQUIRE(world.FindScene("title") != nullptr);
    world.LoadScene("nothing"); // 頼まれたままのものも捨てる
    world.UnloadAllChildScenes();
    CHECK(world.GetRootScene()->children.empty());
    CHECK(world.GetSceneRequests().empty());
    app.StepFrame(16'000'000, {});
    CHECK(FindActor(app, "A") == nullptr);
}
