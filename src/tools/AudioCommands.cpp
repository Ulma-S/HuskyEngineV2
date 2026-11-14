// 【開発ビルドだけ】音のエディタのコマンド。説明は AudioCommands.h。
#if HUSKY_DEVELOPMENT

#include "tools/AudioCommands.h"

#include "audio/AudioDevice.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/SoundAsset.h"

#include <algorithm>
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

    EditorCommand MakeAudioPlayCommand()
    {
        EditorCommand command;
        command.name = "audio.play";
        command.label = "Play sound";
        command.description = "Play a sound asset (.wav). The last sound played this way is kept loaded (so it is not collected while it plays).";
        command.whyUnavailable = [](engine::Application& app) -> const char* {
            return app.GetAudio().HasOutput() ? nullptr : "No audio output (the device could not be opened)";
        };
        EditorCommandArg soundArg{"sound", EditorArgType::String,
                                  "Sound asset: its GUID or its path in the assets folder, like \"sounds/hit.wav\"", true};
        soundArg.choices = [](engine::Application& app, const engine::ArchiveValue&) {
            std::vector<EditorArgChoice> choices;
            app.GetAssetDatabase().ForEachAsset([&choices, &app](const core::Guid& guid, const std::string& path) {
                if (app.GetAssetLoader().CanLoadAs(engine::SoundAsset::StaticType(), path))
                {
                    choices.push_back({guid.ToString(), path});
                }
            });
            std::sort(choices.begin(), choices.end(),
                      [](const EditorArgChoice& a, const EditorArgChoice& b) { return a.label < b.label; });
            return choices;
        };
        EditorCommandArg loopArg{"loop", EditorArgType::Bool, "Loop the sound (default: false)", false};
        loopArg.defaultValue = engine::ArchiveValue(false);
        EditorCommandArg volumeArg{"volume", EditorArgType::Float, "Volume of this play, 0 or more (default: 1)", false};
        volumeArg.defaultValue = engine::ArchiveValue(1.0);
        command.args = {soundArg, loopArg, volumeArg};

        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            const std::string text = context.args.Find("sound")->AsString();
            core::Guid guid;
            if (!core::Guid::Parse(text, guid))
            {
                const core::Guid* found = app.GetAssetDatabase().FindGuid(text);
                if (found == nullptr)
                {
                    return EditorCommandResult::Failure("No asset: " + text);
                }
                guid = *found;
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            const core::ObjectHandle handle = loader.Load<engine::SoundAsset>(guid);
            loader.WaitForAllLoads(); // 試し聞きなので、読み終わるまで待つ(ふつうのゲームでは待たない)
            const engine::SoundAsset* sound = loader.Resolve<engine::SoundAsset>(handle);
            if (sound == nullptr || !sound->IsLoaded())
            {
                return EditorCommandResult::Failure("Could not load the sound: " + text);
            }
            // 鳴っている間に回収されないよう、最後に鳴らしたものを根に入れておく(前のものは根から外す)。
            static core::ObjectHandle s_kept;
            if (s_kept != handle)
            {
                if (!s_kept.IsNull())
                {
                    app.GetRegistry().RemoveRoot(s_kept);
                }
                app.GetRegistry().AddRoot(handle);
                s_kept = handle;
            }

            audio::PlayParams params;
            const engine::ArchiveValue* loop = context.args.Find("loop");
            params.loop = loop != nullptr && loop->AsBool();
            const engine::ArchiveValue* volume = context.args.Find("volume");
            params.volume = volume != nullptr ? static_cast<float>(volume->AsFloat()) : 1.0f;
            params.bus = params.loop ? audio::Bus::Music : audio::Bus::Effect;
            const audio::PlaybackId id = app.GetAudio().Play(sound->GetSound(), params);
            if (id.IsNull())
            {
                return EditorCommandResult::Failure("Could not play the sound: " + text);
            }
            EditorCommandResult result = EditorCommandResult::Success(
                "Playing " + text + " (" + std::to_string(sound->GetDurationSeconds()) + " s" + (params.loop ? ", loop)" : ")"));
            return result;
        };
        return command;
    }

    EditorCommand MakeAudioStopAllCommand()
    {
        EditorCommand command;
        command.name = "audio.stopAll";
        command.label = "Stop all sounds";
        command.description = "Stop every sound that is playing.";
        command.whyUnavailable = [](engine::Application& app) -> const char* {
            return app.GetAudio().HasOutput() ? nullptr : "No audio output (the device could not be opened)";
        };
        command.execute = [](EditorCommandContext& context) {
            context.app.GetAudio().StopAll();
            return EditorCommandResult::Success("Stopped all sounds");
        };
        return command;
    }
}

#endif
