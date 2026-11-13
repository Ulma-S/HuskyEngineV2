// ミキサー(audio::Mixer)のテスト。音を出さずに、Mix が書いた数を確かめる。
#include "audio/Mixer.h"

#include <doctest/doctest.h>

#include <vector>

using husky::audio::Bus;
using husky::audio::Mixer;
using husky::audio::OutputChannels;
using husky::audio::PlayParams;
using husky::audio::SoundData;
using husky::audio::PlaybackId;

namespace
{
    // 左右とも同じ値が frameCount フレーム続く音。
    std::vector<float> MakeConstant(int frameCount, float value)
    {
        return std::vector<float>(static_cast<size_t>(frameCount * OutputChannels), value);
    }

    // 左右とも 1, 2, 3, ... と増える音(どこを読んだか分かるように)。
    std::vector<float> MakeRamp(int frameCount)
    {
        std::vector<float> samples;
        for (int i = 0; i < frameCount; ++i)
        {
            samples.push_back(static_cast<float>(i + 1) * 0.1f);
            samples.push_back(static_cast<float>(i + 1) * 0.1f);
        }
        return samples;
    }

    SoundData MakeSound(const std::vector<float>& samples)
    {
        return SoundData{samples.data(), static_cast<int>(samples.size()) / OutputChannels};
    }
}

TEST_CASE("Mixer: 何も鳴っていなければ 0 を書く(前の中身を上書きする)")
{
    Mixer mixer;
    std::vector<float> out(8, 0.5f);
    mixer.Mix(out.data(), 4);
    for (float sample : out)
    {
        CHECK(sample == 0.0f);
    }
}

TEST_CASE("Mixer: 2つの音は足し合わされ、音量(1回・種類・全体)が掛かる")
{
    Mixer mixer;
    const std::vector<float> a = MakeConstant(4, 0.25f);
    const std::vector<float> b = MakeConstant(4, 0.25f);
    REQUIRE(mixer.Play(PlaybackId{1}, MakeSound(a), PlayParams{}));
    PlayParams half;
    half.volume = 0.5f;
    half.bus = Bus::Music;
    REQUIRE(mixer.Play(PlaybackId{2}, MakeSound(b), half));

    std::vector<float> out(2 * OutputChannels);
    mixer.Mix(out.data(), 2);
    CHECK(out[0] == doctest::Approx(0.25f + 0.125f));

    mixer.SetBusVolume(Bus::Music, 0.0f); // BGM を消す
    mixer.SetMasterVolume(0.5f);
    mixer.Mix(out.data(), 2);
    CHECK(out[0] == doctest::Approx(0.125f));
    CHECK(out[1] == doctest::Approx(0.125f));
}

TEST_CASE("Mixer: 最後まで鳴ると止まり、残りは 0")
{
    Mixer mixer;
    const std::vector<float> ramp = MakeRamp(3);
    REQUIRE(mixer.Play(PlaybackId{7}, MakeSound(ramp), PlayParams{}));

    std::vector<float> out(5 * OutputChannels);
    mixer.Mix(out.data(), 5);
    CHECK(out[0] == doctest::Approx(0.1f));
    CHECK(out[2 * OutputChannels] == doctest::Approx(0.3f));
    CHECK(out[3 * OutputChannels] == 0.0f);
    CHECK_FALSE(mixer.IsPlaying(PlaybackId{7}));
    CHECK(mixer.GetPlayingCount() == 0);
}

TEST_CASE("Mixer: ループする音は最初に戻り、1回の Mix の中でも続けて埋める")
{
    Mixer mixer;
    const std::vector<float> ramp = MakeRamp(2);
    PlayParams loop;
    loop.loop = true;
    REQUIRE(mixer.Play(PlaybackId{1}, MakeSound(ramp), loop));

    std::vector<float> out(5 * OutputChannels);
    mixer.Mix(out.data(), 5); // 0.1, 0.2, 0.1, 0.2, 0.1
    CHECK(out[2 * OutputChannels] == doctest::Approx(0.1f));
    CHECK(out[4 * OutputChannels] == doctest::Approx(0.1f));
    mixer.Mix(out.data(), 1); // 続きから: 0.2
    CHECK(out[0] == doctest::Approx(0.2f));
    CHECK(mixer.IsPlaying(PlaybackId{1}));
}

TEST_CASE("Mixer: 範囲を超えた所は -1〜1 に切り詰める")
{
    Mixer mixer;
    const std::vector<float> loud = MakeConstant(1, 0.8f);
    REQUIRE(mixer.Play(PlaybackId{1}, MakeSound(loud), PlayParams{}));
    REQUIRE(mixer.Play(PlaybackId{2}, MakeSound(loud), PlayParams{}));
    std::vector<float> out(OutputChannels);
    mixer.Mix(out.data(), 1);
    CHECK(out[0] == 1.0f);
}

TEST_CASE("Mixer: Stop と StopAll で止まる。知らない番号や 0 は何もしない")
{
    Mixer mixer;
    const std::vector<float> sound = MakeConstant(100, 0.1f);
    CHECK_FALSE(mixer.Play(PlaybackId{}, MakeSound(sound), PlayParams{})); // 0 は使えない
    CHECK_FALSE(mixer.Play(PlaybackId{1}, SoundData{}, PlayParams{}));     // 空のデータ
    REQUIRE(mixer.Play(PlaybackId{1}, MakeSound(sound), PlayParams{}));
    REQUIRE(mixer.Play(PlaybackId{2}, MakeSound(sound), PlayParams{}));

    mixer.Stop(PlaybackId{99});
    CHECK(mixer.GetPlayingCount() == 2);
    mixer.Stop(PlaybackId{1});
    CHECK_FALSE(mixer.IsPlaying(PlaybackId{1}));
    CHECK(mixer.IsPlaying(PlaybackId{2}));
    mixer.StopAll();
    CHECK(mixer.GetPlayingCount() == 0);
}

TEST_CASE("Mixer: いっぱいのときは一番古い効果音を止める。BGM は止めない")
{
    Mixer mixer;
    const std::vector<float> sound = MakeConstant(100, 0.0f);
    PlayParams music;
    music.bus = Bus::Music;
    REQUIRE(mixer.Play(PlaybackId{1}, MakeSound(sound), music)); // 一番古いが BGM
    for (std::uint32_t i = 2; i <= static_cast<std::uint32_t>(Mixer::MaxVoices); ++i)
    {
        REQUIRE(mixer.Play(PlaybackId{i}, MakeSound(sound), PlayParams{}));
    }
    CHECK(mixer.GetPlayingCount() == Mixer::MaxVoices);

    REQUIRE(mixer.Play(PlaybackId{100}, MakeSound(sound), PlayParams{}));
    CHECK(mixer.IsPlaying(PlaybackId{1}));       // BGM は残る
    CHECK_FALSE(mixer.IsPlaying(PlaybackId{2})); // 一番古い効果音が止まった
    CHECK(mixer.IsPlaying(PlaybackId{100}));
    CHECK(mixer.GetPlayingCount() == Mixer::MaxVoices);
}

TEST_CASE("Mixer: すべて BGM で埋まっていれば、新しい音は鳴らせない")
{
    Mixer mixer;
    const std::vector<float> sound = MakeConstant(10, 0.0f);
    PlayParams music;
    music.bus = Bus::Music;
    for (std::uint32_t i = 1; i <= static_cast<std::uint32_t>(Mixer::MaxVoices); ++i)
    {
        REQUIRE(mixer.Play(PlaybackId{i}, MakeSound(sound), music));
    }
    CHECK_FALSE(mixer.Play(PlaybackId{100}, MakeSound(sound), PlayParams{}));
}
