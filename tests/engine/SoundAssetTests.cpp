// 音のアセット(SoundAsset)とローダの読み込みのテスト。WAV はテストの中で組み立てて、一時フォルダに書く。
#include "engine/SoundAsset.h"

#include "TestFolder.h"
#include "WavBuilder.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::ObjectHandle;
using husky::engine::Application;
using husky::engine::AssetLoader;
using husky::engine::AssetState;
using husky::engine::SoundAsset;
using husky::tests::MakeGuid;
using husky::tests::MakeWav16;
using husky::tests::TestFolder;

namespace
{
    void WriteWav(const TestFolder& folder, const std::string& path, const std::string& bytes, const husky::core::Guid& guid)
    {
        folder.Write(path, bytes);
        folder.Write(path + ".meta", "{\"guid\": \"" + guid.ToString() + "\"}");
    }
}

TEST_CASE("SoundAsset: .wav は SoundAsset として読める")
{
    CHECK(AssetLoader::CanLoadAs(SoundAsset::StaticType(), "sounds/hit.wav"));
    CHECK_FALSE(AssetLoader::CanLoadAs(SoundAsset::StaticType(), "sounds/hit.ogg"));
}

TEST_CASE("SoundAsset: 読み終わると長さが分かり、波は音の層に預けられる。回収されると手放す")
{
    TestFolder folder("husky_test_sound_asset");
    // 24000Hz のモノラル 2 フレーム → 48000Hz では 4 フレーム
    WriteWav(folder, "sounds/beep.wav", MakeWav16(24000, 1, {16384, 0}), MakeGuid(5, 1));

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();

    const ObjectHandle handle = loader.LoadSound(MakeGuid(5, 1));
    const SoundAsset* sound = loader.ResolveSound(handle);
    REQUIRE(sound != nullptr);
    CHECK(sound->GetState() == AssetState::Loading);
    loader.WaitForAllLoads();
    REQUIRE(sound->IsLoaded());
    CHECK(sound->GetFrameCount() == 4);
    REQUIRE_FALSE(sound->GetSound().IsNull());
    CHECK(app.GetAudio().GetSoundFrameCount(sound->GetSound()) == 4);
    CHECK(loader.LoadSound(MakeGuid(5, 1)) == handle); // 同じ GUID は同じアセット

    // どこからも強く指されていないので回収され、波も手放す(出す先が無いので、その場で捨てる)。
    app.GetRegistry().CollectGarbage();
    CHECK(loader.ResolveSound(handle) == nullptr);
    CHECK(loader.GetLoadedSoundCount() == 0);
    CHECK(app.GetAudio().GetSoundCount() == 0);
}

TEST_CASE("SoundAsset: WAV として読めなければ「失敗」")
{
    TestFolder folder("husky_test_sound_asset_broken");
    WriteWav(folder, "sounds/broken.wav", "not a wav", MakeGuid(5, 2));

    Application app;
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    const ObjectHandle handle = app.GetAssetLoader().LoadSound(MakeGuid(5, 2));
    app.GetAssetLoader().WaitForAllLoads();
    const SoundAsset* sound = app.GetAssetLoader().ResolveSound(handle);
    REQUIRE(sound != nullptr);
    CHECK(sound->GetState() == AssetState::Failed);
    CHECK(sound->GetSound().IsNull());
}
