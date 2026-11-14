// シーンのエディタのコマンド(SC-4): scene.open / load / unload / addChild / removeChild / list と、scene.save が子のシーンも書くこと。
#if HUSKY_DEVELOPMENT

#include "TestFolder.h"
#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/JsonFormat.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"
#include "platform/FileSystem.h"
#include "tools/EditorCommands.h"
#include "tools/SceneCommands.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::core::Guid;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::ArchiveValue;
using husky::engine::Scene;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;
using husky::tools::EditorCommandRegistry;
using husky::tools::EditorCommandResult;

namespace
{
    ArchiveValue Args(const std::string& json)
    {
        ArchiveValue value;
        REQUIRE(husky::engine::JsonFormat::Read(json, value, nullptr));
        return value;
    }

    // 名前を付けたアクターと、子のシーンの記録を持つシーンのファイルの文字列。
    std::string MakeScene(const std::vector<std::string>& names, const std::vector<Scene::ChildSceneEntry>& children = {})
    {
        Application source;
        for (const std::string& actorName : names)
        {
            source.GetWorld().Spawn<Actor>()->name = actorName;
        }
        Scene* root = source.GetWorld().GetRootScene();
        root->childScenes = children;
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(husky::engine::SceneSerializer::WriteScene(source.GetWorld(), *root).value, text,
                                                 true));
        return text;
    }

    void WriteScene(TestFolder& folder, const std::string& path, const Guid& guid, const std::string& text)
    {
        folder.Write(path, text);
        folder.Write(path + ".meta", "{\"guid\": \"" + guid.ToString() + "\"}");
    }

    // master(Master)の中に system(Bgm、load)と game(Player、load しない)。title(Logo)は一覧にだけある。
    struct Fixture
    {
        TestFolder folder{"husky_test_scene_commands"};
        Application app;
        EditorCommandRegistry commands;

        Fixture()
        {
            WriteScene(folder, "scenes/system.scene", MakeGuid(15, 2), MakeScene({"Bgm"}));
            WriteScene(folder, "scenes/game.scene", MakeGuid(15, 3), MakeScene({"Player"}));
            WriteScene(folder, "scenes/title.scene", MakeGuid(15, 4), MakeScene({"Logo"}));
            WriteScene(folder, "scenes/master.scene", MakeGuid(15, 1),
                       MakeScene({"Master"}, {{MakeGuid(15, 2), true}, {MakeGuid(15, 3), false}}));
            folder.Write("settings/scenes.sdata",
                         "{\"type\": \"SceneList\", \"properties\": {\"scenes\": [\"" + MakeGuid(15, 1).ToString() + "\", \"" +
                             MakeGuid(15, 2).ToString() + "\", \"" + MakeGuid(15, 3).ToString() + "\", \"" + MakeGuid(15, 4).ToString() +
                             "\"]}}");
            folder.Write("settings/scenes.sdata.meta", "{\"guid\": \"" + MakeGuid(15, 9).ToString() + "\"}");
            REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
            REQUIRE(app.LoadSceneList(MakeGuid(15, 9)));
            REQUIRE(commands.Register(husky::tools::MakeOpenSceneCommand(commands)));
            REQUIRE(commands.Register(husky::tools::MakeLoadSceneCommand()));
            REQUIRE(commands.Register(husky::tools::MakeUnloadSceneCommand()));
            REQUIRE(commands.Register(husky::tools::MakeAddChildSceneCommand()));
            REQUIRE(commands.Register(husky::tools::MakeRemoveChildSceneCommand()));
            REQUIRE(commands.Register(husky::tools::MakeListScenesCommand()));
            REQUIRE(commands.Register(husky::tools::MakeSaveSceneCommand({})));
            REQUIRE(commands.Register(husky::tools::MakeUndoCommand(commands)));
            REQUIRE(commands.Register(husky::tools::MakeRedoCommand(commands)));
        }

        EditorCommandResult Run(const std::string& name, const std::string& json = "{}")
        {
            return commands.Execute(name, app, Args(json));
        }
    };
}

TEST_CASE("SceneCommands: scene.open は一覧の名前かパスでルートとして開き、F5 の保存先も替わる(SC-4)")
{
    Fixture f;
    CHECK_FALSE(f.Run("scene.open", R"({"scene": "nothing"})").succeeded);
    const EditorCommandResult opened = f.Run("scene.open", R"({"scene": "master"})");
    REQUIRE(opened.succeeded);
    Scene* root = f.app.GetWorld().GetRootScene();
    CHECK(root->name == "master");
    CHECK(f.app.GetWorld().FindScene("system") != nullptr); // load の印
    CHECK(f.app.GetWorld().FindScene("game") == nullptr);
    CHECK(f.app.GetRootScenePath() == f.folder.PathOf("scenes/master.scene"));

    // パスでも開ける(一覧に無くてよい)
    REQUIRE(f.Run("scene.open", R"({"scene": "scenes/title.scene"})").succeeded);
    CHECK(f.app.GetWorld().GetRootScene()->name == "title");
    CHECK(f.app.GetWorld().GetRootScene()->asset == MakeGuid(15, 4));
    CHECK(f.app.GetRootScenePath() == f.folder.PathOf("scenes/title.scene"));
}

TEST_CASE("SceneCommands: scene.load / scene.unload は子のシーンをその場で読む・下ろす。ルートシーンは下ろせない(SC-4)")
{
    Fixture f;
    REQUIRE(f.Run("scene.open", R"({"scene": "master"})").succeeded);
    CHECK_FALSE(f.Run("scene.load", R"({"scene": "nothing"})").succeeded);
    CHECK_FALSE(f.Run("scene.load", R"({"scene": "system"})").succeeded); // もう読んである
    REQUIRE(f.Run("scene.load", R"({"scene": "game"})").succeeded);
    REQUIRE(f.Run("scene.load", R"({"scene": "title", "parent": "game"})").succeeded);
    CHECK(f.app.GetWorld().FindScene("title")->GetParent() == f.app.GetWorld().FindScene("game"));

    const EditorCommandResult listed = f.Run("scene.list");
    REQUIRE(listed.succeeded);
    CHECK(listed.message == "4 scene(s) loaded");
    CHECK(listed.data.GetArraySize() == 4);

    CHECK_FALSE(f.Run("scene.unload", R"({"scene": "master"})").succeeded);
    REQUIRE(f.Run("scene.unload", R"({"scene": "game"})").succeeded);
    CHECK(f.app.GetWorld().FindScene("game") == nullptr);
    CHECK(f.app.GetWorld().FindScene("title") == nullptr); // 子のシーンも一緒に
}

TEST_CASE("SceneCommands: scene.addChild / scene.removeChild はルートの記録を書き換え、Undo / Redo できる(SC-4)")
{
    Fixture f;
    REQUIRE(f.Run("scene.open", R"({"scene": "master"})").succeeded);
    Scene* root = f.app.GetWorld().GetRootScene();
    REQUIRE(root->childScenes.size() == 2);

    CHECK_FALSE(f.Run("scene.addChild", R"({"child": "game"})").succeeded);   // もう記録がある
    CHECK_FALSE(f.Run("scene.addChild", R"({"child": "master"})").succeeded); // 自分自身
    CHECK_FALSE(f.Run("scene.addChild", R"({"child": "nothing"})").succeeded);
    REQUIRE(f.Run("scene.addChild", R"({"child": "title", "load": false})").succeeded);
    REQUIRE(root->childScenes.size() == 3);
    CHECK(root->childScenes[2].scene == MakeGuid(15, 4));
    CHECK_FALSE(root->childScenes[2].loadOnOpen);
    CHECK(f.app.GetWorld().FindScene("title") == nullptr); // 記録だけで、読みはしない

    // 真ん中の記録を外して Undo → 同じ位置・同じ印で戻る
    REQUIRE(f.Run("scene.removeChild", R"({"child": "game"})").succeeded);
    CHECK(root->childScenes.size() == 2);
    REQUIRE(f.Run("editor.undo").succeeded);
    REQUIRE(root->childScenes.size() == 3);
    CHECK(root->childScenes[1].scene == MakeGuid(15, 3));
    CHECK_FALSE(root->childScenes[1].loadOnOpen);
    REQUIRE(f.Run("editor.redo").succeeded);
    CHECK(root->childScenes.size() == 2);

    // scene.open は履歴を空にする
    REQUIRE(f.Run("scene.open", R"({"scene": "master"})").succeeded);
    CHECK(f.commands.GetUndoCount() == 0);
}

TEST_CASE("SceneCommands: scene.save はルートシーンと読んでいる子のシーンを、それぞれのファイルに書く(SC-4)")
{
    Fixture f;
    REQUIRE(f.Run("scene.open", R"({"scene": "master"})").succeeded);
    // ルートに置いたものはルートのファイル、system に置いたものは system のファイルへ
    f.app.GetWorld().Spawn<Actor>()->name = "NewInRoot";
    f.app.GetWorld().SpawnInScene<Actor>(*f.app.GetWorld().FindScene("system"))->name = "NewInSystem";
    REQUIRE(f.Run("scene.addChild", R"({"child": "title", "load": true})").succeeded);
    const EditorCommandResult saved = f.Run("scene.save");
    REQUIRE(saved.succeeded);
    CHECK(saved.details.size() == 2); // master と system

    std::string masterText;
    std::string systemText;
    REQUIRE(husky::platform::ReadTextFile(f.folder.PathOf("scenes/master.scene").c_str(), masterText));
    REQUIRE(husky::platform::ReadTextFile(f.folder.PathOf("scenes/system.scene").c_str(), systemText));
    CHECK(masterText.find("NewInRoot") != std::string::npos);
    CHECK(masterText.find("NewInSystem") == std::string::npos);
    CHECK(systemText.find("NewInSystem") != std::string::npos);
    CHECK(systemText.find("Bgm") != std::string::npos);

    // 開き直すと、新しい記録(title、load)も読まれる
    REQUIRE(f.Run("scene.open", R"({"scene": "master"})").succeeded);
    CHECK(f.app.GetWorld().FindScene("title") != nullptr);
    CHECK(f.app.GetWorld().GetRootScene()->childScenes.size() == 3);
}

#endif
