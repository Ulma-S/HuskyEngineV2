// WAV を読む・ミキサーの形に直す(audio/WavFile.h)のテスト。WAV のバイト列はテストの中で組み立てる。
#include "audio/WavFile.h"

#include "WavBuilder.h"
#include "audio/Mixer.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using husky::audio::ConvertToOutputFormat;
using husky::audio::DecodedSound;
using husky::audio::DecodeWav;
using husky::audio::OutputChannels;
using husky::audio::OutputSampleRate;
using husky::tests::MakeWav16;
using husky::tests::MakeWavRaw;

namespace
{
    bool Decode(const std::string& bytes, DecodedSound& sound, const char*& error)
    {
        return DecodeWav(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), sound, error);
    }
}

TEST_CASE("WavFile: 16 ビットのステレオを読む")
{
    const std::string bytes = MakeWav16(44100, 2, {16384, -16384, 0, 32767});
    DecodedSound sound;
    const char* error = nullptr;
    REQUIRE(Decode(bytes, sound, error));
    CHECK(sound.sampleRate == 44100);
    CHECK(sound.channels == 2);
    CHECK(sound.GetFrameCount() == 2);
    CHECK(sound.samples[0] == doctest::Approx(0.5f));
    CHECK(sound.samples[1] == doctest::Approx(-0.5f));
    CHECK(sound.samples[3] == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("WavFile: 8 ビット(符号なし)、24 ビット、float を読む")
{
    DecodedSound sound;
    const char* error = nullptr;

    // 8 ビット: 128 が 0、0 が -1
    REQUIRE(Decode(MakeWavRaw(1, 8000, 1, 8, {128, 0}), sound, error));
    CHECK(sound.samples[0] == 0.0f);
    CHECK(sound.samples[1] == -1.0f);

    // 24 ビット: 0x400000 が 0.5(小さいバイトが先)
    REQUIRE(Decode(MakeWavRaw(1, 8000, 1, 24, {0x00, 0x00, 0x40}), sound, error));
    CHECK(sound.samples[0] == doctest::Approx(0.5f));

    // float: 0.25f のビットの並び(0x3E800000)
    REQUIRE(Decode(MakeWavRaw(3, 8000, 1, 32, {0x00, 0x00, 0x80, 0x3E}), sound, error));
    CHECK(sound.samples[0] == doctest::Approx(0.25f));
}

TEST_CASE("WavFile: 知らないチャンクは読み飛ばす")
{
    std::string bytes = MakeWav16(48000, 1, {8192});
    // "fmt " の前に LIST チャンク(中身 3 バイト + 詰め物 1 バイト)を差し込む
    const std::string list = std::string("LIST") + std::string("\x03\x00\x00\x00", 4) + "abc" + std::string(1, '\0');
    bytes.insert(12, list);
    DecodedSound sound;
    const char* error = nullptr;
    REQUIRE(Decode(bytes, sound, error));
    CHECK(sound.samples[0] == doctest::Approx(0.25f));
}

TEST_CASE("WavFile: 読めないものは理由つきで断る")
{
    DecodedSound sound;
    const char* error = nullptr;
    CHECK_FALSE(Decode("not a wav file", sound, error));
    CHECK(error != nullptr);

    error = nullptr;
    CHECK_FALSE(Decode(MakeWavRaw(2, 8000, 1, 4, {0}), sound, error)); // ADPCM など
    CHECK(error != nullptr);

    error = nullptr;
    CHECK_FALSE(Decode(MakeWavRaw(1, 8000, 6, 16, {0, 0}), sound, error)); // 6 チャンネル
    CHECK(error != nullptr);
}

TEST_CASE("WavFile: モノラルは左右に同じ値を入れ、1 秒のフレーム数が同じならそのまま")
{
    DecodedSound sound;
    sound.sampleRate = OutputSampleRate;
    sound.channels = 1;
    sound.samples = {0.1f, 0.2f, 0.3f};
    const std::vector<float> out = ConvertToOutputFormat(sound);
    REQUIRE(out.size() == 3 * OutputChannels);
    CHECK(out[0] == doctest::Approx(0.1f));
    CHECK(out[1] == doctest::Approx(0.1f));
    CHECK(out[4] == doctest::Approx(0.3f));
}

TEST_CASE("WavFile: 1 秒のフレーム数が違えば、間を直線で結んで作る")
{
    DecodedSound sound;
    sound.sampleRate = OutputSampleRate / 2; // 半分 → 長さは 2 倍
    sound.channels = 2;
    sound.samples = {0.0f, 1.0f, 1.0f, 0.0f}; // 2 フレーム
    const std::vector<float> out = ConvertToOutputFormat(sound);
    REQUIRE(out.size() == 4 * OutputChannels);
    CHECK(out[0] == doctest::Approx(0.0f)); // 0 フレーム目 = 元の 0
    CHECK(out[2] == doctest::Approx(0.5f)); // 1 フレーム目 = 元の 0 と 1 の真ん中
    CHECK(out[3] == doctest::Approx(0.5f));
    CHECK(out[4] == doctest::Approx(1.0f)); // 2 フレーム目 = 元の 1
    CHECK(out[6] == doctest::Approx(1.0f)); // 最後は元の最後のまま
}
