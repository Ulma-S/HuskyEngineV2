// 音を鳴らすコンポーネント(AudioSourceComponent)と PlaySoundOnce のテスト。
// Application の音の窓口に、手で呼ぶ出す先(ManualOutput)を付けて、鳴っているかを確かめる。
#include "engine/AudioSourceComponent.h"

#include "ManualAudioOutput.h"
#include "TestFolder.h"
#include "WavBuilder.h"
#include "engine/Actor.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/World.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::AudioSourceComponent;
using husky::tests::MakeGuid;
using husky::tests::MakeWav16;
using husky::tests::ManualOutput;
using husky::tests::ManualOutputState;
using husky::tests::TestFolder;

namespace
{
    const std::vector<husky::platform::KeyEvent> NoEvents;

    // 0.5 の値が 100 フレーム(48000Hz のモノラル)続く音を書き、表を作る。
    void PrepareSound(TestFolder& folder, Application& app)
    {
        std::string data;
        for (int i = 0; i < 100; ++i)
        {
            husky::tests::AppendU16(data, 16384);
        }
        std::string bytes = husky::tests::MakeWavRaw(1, 48000, 1, 16, {});
        bytes.replace(bytes.size() - 4, 4, std::string());
        husky::tests::AppendU32(bytes, static_cast<std::uint32_t>(data.size()));
        bytes += data;
        std::string riffSize;
        husky::tests::AppendU32(riffSize, static_cast<std::uint32_t>(bytes.size() - 8));
        bytes.replace(4, 4, riffSize);
        folder.Write("sounds/tone.wav", bytes);
        folder.Write("sounds/tone.wav.meta", "{\"guid\": \"" + MakeGuid(9, 1).ToString() + "\"}");
        REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    }
}

TEST_CASE("AudioSourceComponent: playOnStart なら、読み終わるのを待ってから鳴らす。壊れると止まる")
{
    TestFolder folder("husky_test_audio_source");
    Application app;
    PrepareSound(folder, app);
    ManualOutputState state;
    REQUIRE(app.GetAudio().StartOutput(std::make_unique<ManualOutput>(state, true)));

    Actor* actor = app.GetWorld().Spawn<Actor>();
    AudioSourceComponent* source = actor->AddComponent<AudioSourceComponent>();
    source->sound.guid = MakeGuid(9, 1);
    source->loop = true;
    source->playOnStart = true;

    app.StepFrame(0, NoEvents); // 最初の Tick: 読み始めるだけ(まだ読み込み中)
    CHECK(source->IsPlaying()); // 待っている間も「鳴っている」
    app.GetAssetLoader().WaitForAllLoads();
    app.StepFrame(0, NoEvents); // 読み終わったので鳴らす
    CHECK(source->IsPlaying());
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));

    source->Stop();
    CHECK_FALSE(source->IsPlaying());
    CHECK(state.Render(1)[0] == 0.0f);

    source->Play();
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));
    actor->Destroy();
    app.StepFrame(0, NoEvents); // フレームの終わりにアクターとコンポーネントが壊れ、音を止める
    app.GetRegistry().CollectGarbage();
    CHECK(state.Render(1)[0] == 0.0f);
    app.GetAudio().StopOutput(); // 出す先の様子(state)より先に閉じる
}

TEST_CASE("PlaySoundOnce: 読み終わっていなければ鳴らさず、読み終わっていれば鳴らす")
{
    TestFolder folder("husky_test_play_sound_once");
    Application app;
    PrepareSound(folder, app);
    ManualOutputState state;
    REQUIRE(app.GetAudio().StartOutput(std::make_unique<ManualOutput>(state, true)));

    // AssetRef を持つもの(ここではアクターのコンポーネントの代わりに根に入れた音を指す参照)が、鳴り終わるまで生きていること。
    husky::core::AssetRef<husky::engine::SoundAsset> sound;
    sound.guid = MakeGuid(9, 1);
    CHECK(husky::engine::PlaySoundOnce(app.GetWorld(), sound).IsNull()); // 読み始めただけ
    app.GetRegistry().AddRoot(sound.asset);
    app.GetAssetLoader().WaitForAllLoads();

    const husky::audio::PlaybackId id = husky::engine::PlaySoundOnce(app.GetWorld(), sound, 0.5f);
    REQUIRE_FALSE(id.IsNull());
    CHECK(state.Render(1)[0] == doctest::Approx(0.25f));
    app.GetAudio().StopOutput();
}

TEST_CASE("AudioSourceComponent: 音の種類は名前で保存される")
{
    const husky::core::EnumInfo* info = husky::core::EnumInfoOf<husky::audio::Bus>();
    REQUIRE(info != nullptr);
    CHECK(std::string(info->FindName(static_cast<std::int64_t>(husky::audio::Bus::Music))) == "Music");
}
