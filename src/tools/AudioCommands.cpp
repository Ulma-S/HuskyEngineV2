// 【開発ビルドだけ】音のエディタのコマンド。説明は AudioCommands.h。
#if HUSKY_DEVELOPMENT

#include "tools/AudioCommands.h"

#include "audio/AudioDevice.h"
#include "engine/Application.h"

#include <cmath>
#include <vector>

namespace husky::tools
{
    namespace
    {
        // 試しの音のデータ。最初に使うときに1回だけ作り、プログラムの終わりまで持つ
        // (鳴っている間にデータを壊さないため。音のスレッドが読むので、作った後は書き換えない)。
        const std::vector<float>& GetTestToneSamples()
        {
            static const std::vector<float> samples = [] {
                constexpr float frequency = 440.0f; // ラの音
                constexpr float seconds = 0.3f;
                constexpr float amplitude = 0.3f;
                constexpr float pi = 3.14159265f;
                const int frameCount = static_cast<int>(seconds * audio::OutputSampleRate);
                const int fadeFrames = audio::OutputSampleRate / 100; // 最初と最後の 10ms で音量を上げ下げする(ぶつっと鳴らないように)
                std::vector<float> result(static_cast<size_t>(frameCount * audio::OutputChannels));
                for (int i = 0; i < frameCount; ++i)
                {
                    float envelope = 1.0f;
                    if (i < fadeFrames)
                    {
                        envelope = static_cast<float>(i) / fadeFrames;
                    }
                    else if (i > frameCount - fadeFrames)
                    {
                        envelope = static_cast<float>(frameCount - i) / fadeFrames;
                    }
                    const float value = amplitude * envelope *
                                        std::sin(2.0f * pi * frequency * static_cast<float>(i) / audio::OutputSampleRate);
                    result[static_cast<size_t>(i * audio::OutputChannels)] = value;     // 左
                    result[static_cast<size_t>(i * audio::OutputChannels + 1)] = value; // 右
                }
                return result;
            }();
            return samples;
        }
    }

    EditorCommand MakeAudioTestToneCommand()
    {
        EditorCommand command;
        command.name = "audio.testTone";
        command.label = "Play test tone";
        command.description = "Play a short test tone (440 Hz, 0.3 s) to check that the audio output works.";
        command.whyUnavailable = [](engine::Application& app) -> const char* {
            return app.GetAudio().HasOutput() ? nullptr : "No audio output (the device could not be opened)";
        };
        command.execute = [](EditorCommandContext& context) {
            const std::vector<float>& samples = GetTestToneSamples();
            const audio::SoundData sound{samples.data(), static_cast<int>(samples.size()) / audio::OutputChannels};
            const audio::PlaybackId id = context.app.GetAudio().Play(sound, audio::PlayParams{});
            if (id.IsNull())
            {
                return EditorCommandResult::Failure("Could not play the test tone");
            }
            return EditorCommandResult::Success("Playing test tone");
        };
        return command;
    }
}

#endif
