// 音を鳴らす窓口(audio::AudioDevice)のテスト。
// 本物の装置は使わず、テストが手で「次の N フレームを書いて」を呼ぶ出す先(ManualOutput)を付ける。
// 本物では音のスレッドが呼ぶところを、テストのスレッドが呼ぶので、結果は毎回同じになる。
#include "audio/AudioDevice.h"

#include <doctest/doctest.h>

#include <memory>
#include <vector>

using husky::audio::AudioDevice;
using husky::audio::AudioOutput;
using husky::audio::Bus;
using husky::audio::OutputChannels;
using husky::audio::PlaybackId;
using husky::audio::PlayParams;
using husky::audio::RenderCallback;
using husky::audio::SoundData;

namespace
{
    // 出す先の様子。出す先は AudioDevice が持って壊すので、テストはこちらを見る。
    struct ManualOutputState
    {
        RenderCallback callback = nullptr;
        void* user = nullptr;
        bool started = false;
        bool stopped = false;

        // 本物の音のスレッドの代わりに呼ぶ。
        std::vector<float> Render(int frameCount)
        {
            std::vector<float> out(static_cast<size_t>(frameCount * OutputChannels), -9.0f);
            REQUIRE(callback != nullptr);
            callback(user, out.data(), frameCount);
            return out;
        }
    };

    class ManualOutput final : public AudioOutput
    {
    public:
        ManualOutput(ManualOutputState& state, bool canStart) : m_state(state), m_canStart(canStart) {}

        bool Start(RenderCallback callback, void* user) override
        {
            if (!m_canStart)
            {
                return false;
            }
            m_state.callback = callback;
            m_state.user = user;
            m_state.started = true;
            return true;
        }
        void Stop() override
        {
            m_state.stopped = true;
            m_state.callback = nullptr;
        }
        const char* GetName() const override { return "manual"; }

    private:
        ManualOutputState& m_state;
        bool m_canStart;
    };

    std::vector<float> MakeConstant(int frameCount, float value)
    {
        return std::vector<float>(static_cast<size_t>(frameCount * OutputChannels), value);
    }

    SoundData MakeSound(const std::vector<float>& samples)
    {
        return SoundData{samples.data(), static_cast<int>(samples.size()) / OutputChannels};
    }
}

TEST_CASE("AudioDevice: 出す先が無ければ、Play は null を返して何もしない")
{
    AudioDevice audio;
    const std::vector<float> sound = MakeConstant(4, 0.5f);
    const PlaybackId id = audio.Play(MakeSound(sound), PlayParams{});
    CHECK(id.IsNull());
    CHECK_FALSE(audio.IsPlaying(id));
    CHECK_FALSE(audio.HasOutput());
}

TEST_CASE("AudioDevice: 出す先が開けなければ、出す先なしのまま")
{
    AudioDevice audio;
    ManualOutputState state;
    CHECK_FALSE(audio.StartOutput(std::make_unique<ManualOutput>(state, false)));
    CHECK_FALSE(audio.HasOutput());
}

TEST_CASE("AudioDevice: 積んだ命令は、次に混ぜるときに当てはまる")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    REQUIRE(state.started);

    const std::vector<float> sound = MakeConstant(3, 0.5f);
    const PlaybackId id = audio.Play(MakeSound(sound), PlayParams{});
    REQUIRE_FALSE(id.IsNull());
    CHECK(audio.IsPlaying(id)); // 積んだだけでも「鳴っている」

    std::vector<float> out = state.Render(2);
    CHECK(out[0] == doctest::Approx(0.5f));
    CHECK(audio.IsPlaying(id)); // あと 1 フレーム残っている

    out = state.Render(2);
    CHECK(out[0] == doctest::Approx(0.5f));
    CHECK(out[OutputChannels] == 0.0f);
    CHECK_FALSE(audio.IsPlaying(id)); // 最後まで鳴った
    CHECK(audio.GetRenderedFrameCount() == 4);
}

TEST_CASE("AudioDevice: 番号は 1 回ごとに違い、混ぜる前に Stop すれば鳴らない")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const std::vector<float> sound = MakeConstant(10, 0.5f);

    const PlaybackId a = audio.Play(MakeSound(sound), PlayParams{});
    const PlaybackId b = audio.Play(MakeSound(sound), PlayParams{});
    CHECK(a != b);
    audio.Stop(a);
    CHECK_FALSE(audio.IsPlaying(a));
    CHECK(audio.IsPlaying(b));

    const std::vector<float> out = state.Render(1);
    CHECK(out[0] == doctest::Approx(0.5f)); // b だけ

    audio.StopAll();
    CHECK_FALSE(audio.IsPlaying(b));
    CHECK(state.Render(1)[0] == 0.0f);
}

TEST_CASE("AudioDevice: 種類の音量は、出す先を開く前に決めても効く")
{
    AudioDevice audio;
    audio.SetBusVolume(Bus::Effect, 0.5f);
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const std::vector<float> sound = MakeConstant(1, 0.5f);
    audio.Play(MakeSound(sound), PlayParams{});
    CHECK(state.Render(1)[0] == doctest::Approx(0.25f));

    audio.SetMasterVolume(0.0f); // 開いている間は命令として積む
    audio.Play(MakeSound(sound), PlayParams{});
    CHECK(state.Render(1)[0] == 0.0f);
}

TEST_CASE("AudioDevice: 行列がいっぱいなら捨てて null を返す")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const std::vector<float> sound = MakeConstant(1, 0.0f);
    for (int i = 0; i < AudioDevice::MaxPendingCommands; ++i)
    {
        REQUIRE_FALSE(audio.Play(MakeSound(sound), PlayParams{}).IsNull());
    }
    CHECK(audio.Play(MakeSound(sound), PlayParams{}).IsNull());

    state.Render(1); // 音のスレッドが取り出すと、また積める
    CHECK_FALSE(audio.Play(MakeSound(sound), PlayParams{}).IsNull());
}

TEST_CASE("AudioDevice: StopOutput で出す先を止め、鳴っていた音も止まる")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const std::vector<float> sound = MakeConstant(100, 0.5f);
    const PlaybackId id = audio.Play(MakeSound(sound), PlayParams{});
    state.Render(1);
    REQUIRE(audio.IsPlaying(id));

    audio.StopOutput();
    CHECK(state.stopped);
    CHECK_FALSE(audio.HasOutput());
    CHECK_FALSE(audio.IsPlaying(id));
}

TEST_CASE("AudioDevice: 預けたデータを番号で鳴らし、出す先が無ければ手放すとすぐ捨てる")
{
    AudioDevice audio;
    const husky::audio::SoundHandle empty = audio.CreateSound({});
    CHECK(empty.IsNull());

    const husky::audio::SoundHandle sound = audio.CreateSound(MakeConstant(5, 0.5f));
    REQUIRE_FALSE(sound.IsNull());
    CHECK(audio.GetSoundFrameCount(sound) == 5);
    CHECK(audio.GetSoundCount() == 1);
    audio.ReleaseSound(sound);
    CHECK(audio.GetSoundCount() == 0);
    CHECK(audio.GetSoundFrameCount(sound) == 0);
}

TEST_CASE("AudioDevice: 鳴っている間に手放すと音が止まり、音のスレッドが当てはめた後の Update で捨てる")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const husky::audio::SoundHandle sound = audio.CreateSound(MakeConstant(100, 0.5f));
    const PlaybackId id = audio.Play(sound, PlayParams{});
    REQUIRE_FALSE(id.IsNull());
    CHECK(state.Render(1)[0] == doctest::Approx(0.5f));

    audio.ReleaseSound(sound);
    CHECK(audio.Play(sound, PlayParams{}).IsNull()); // 手放したものは鳴らせない
    audio.Update();
    CHECK(audio.GetSoundCount() == 1); // 音のスレッドがまだ受け取っていないので、捨てない

    CHECK(state.Render(1)[0] == 0.0f); // 止まった
    CHECK_FALSE(audio.IsPlaying(id));
    audio.Update();
    CHECK(audio.GetSoundCount() == 0);
}

TEST_CASE("AudioDevice: 手放し待ちのデータは、出す先を閉じたときに捨てる")
{
    AudioDevice audio;
    ManualOutputState state;
    REQUIRE(audio.StartOutput(std::make_unique<ManualOutput>(state, true)));
    const husky::audio::SoundHandle sound = audio.CreateSound(MakeConstant(10, 0.5f));
    audio.ReleaseSound(sound);
    CHECK(audio.GetSoundCount() == 1);
    audio.StopOutput();
    CHECK(audio.GetSoundCount() == 0);
}
