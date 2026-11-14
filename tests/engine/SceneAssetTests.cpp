// シーンのアセット(SC-1): `.scene` をローダで読み、中身の値の木から今のシーンへアクターを足せる。
#include "TestFolder.h"
#include "core/AssetRef.h"
#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/JsonFormat.h"
#include "engine/SceneAsset.h"
#include "engine/SceneSerializer.h"

#include <doctest/doctest.h>

#include <string>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::AssetLoader;
using husky::engine::AssetState;
using husky::engine::SceneAsset;
using husky::tests::MakeGuid;
using husky::tests::TestFolder;

namespace
{
    // 名前を付けたアクターを2体持つシーンを、シーンのファイルの文字列にする。
    std::string MakeSceneText()
    {
        Application source;
        source.GetWorld().Spawn<Actor>()->name = "Title";
        source.GetWorld().Spawn<Actor>()->name = "StartButton";
        std::string text;
        REQUIRE(husky::engine::JsonFormat::Write(husky::engine::SceneSerializer::Write(source.GetWorld()).value, text, true));
        return text;
    }
}

TEST_CASE("SceneAsset: .scene を読み、中身からアクターを今のシーンへ足せる(SC-1)")
{
    TestFolder folder("husky_test_scene_asset");
    folder.Write("scenes/title.scene", MakeSceneText());
    folder.Write("scenes/title.scene.meta", "{\"guid\": \"" + MakeGuid(10, 1).ToString() + "\"}");

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();
    CHECK(loader.CanLoadAs(SceneAsset::StaticType(), "scenes/title.scene"));
    CHECK_FALSE(loader.CanLoadAs(SceneAsset::StaticType(), "scenes/title.json"));

    husky::core::AssetRef<SceneAsset> reference;
    reference.guid = MakeGuid(10, 1);
    const SceneAsset* scene = loader.Resolve(reference);
    REQUIRE(scene != nullptr);
    CHECK(scene->GetState() == AssetState::Loading); // 読むのはジョブ
    loader.WaitForAllLoads();
    REQUIRE(scene->IsLoaded());

    // 読み込んだ中身は何度でも使える(アセットは変わらないデータ)。2回足せば4体。
    app.AddSceneData(scene->GetData(), "title.scene");
    REQUIRE(app.GetWorld().actors.size() == 2);
    CHECK(app.GetRegistry().Resolve<Actor>(app.GetWorld().actors[0])->name == "Title");
    app.AddSceneData(scene->GetData(), "title.scene");
    CHECK(app.GetWorld().actors.size() == 4);
}

TEST_CASE("SceneAsset: 壊れた JSON のシーンは「失敗」(SC-1)")
{
    TestFolder folder("husky_test_scene_asset_broken");
    folder.Write("scenes/broken.scene", "{ not json");
    folder.Write("scenes/broken.scene.meta", "{\"guid\": \"" + MakeGuid(10, 2).ToString() + "\"}");

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();
    const SceneAsset* scene = loader.Resolve<SceneAsset>(loader.Load<SceneAsset>(MakeGuid(10, 2)));
    REQUIRE(scene != nullptr);
    loader.WaitForAllLoads();
    CHECK(scene->GetState() == AssetState::Failed);
    CHECK(scene->GetData().IsNull());
}
