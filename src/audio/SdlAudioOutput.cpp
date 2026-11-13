// 音の出す先の SDL3 の中身。audio の中で SDL を触るのは、このファイルだけ。
// SDL をやめるときは、このファイルを別の中身に書き直す(AudioOutput の窓口は変えない)。
#include "audio/AudioLog.h"
#include "audio/AudioOutput.h"
#include "audio/Mixer.h" // OutputChannels、OutputSampleRate

#include <SDL3/SDL.h>

#include <algorithm>

namespace husky::audio
{
    namespace
    {
        // SDL の音のストリーム(SDL_AudioStream)を1本開き、足りなくなったら呼ばれる関数(OnRequest)でミキサーの結果を入れる。
        class SdlAudioOutput final : public AudioOutput
        {
        public:
            ~SdlAudioOutput() override { Stop(); }

            bool Start(RenderCallback callback, void* user) override
            {
                if (m_stream != nullptr)
                {
                    return true;
                }
                // 音の機能は、ウィンドウ(platform::Initialize の SDL_Init)とは別に、使うときだけ始める。
                // SDL は始めた回数を数えているので、Stop の SDL_QuitSubSystem と組にする。
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                {
                    HUSKY_LOG(AudioLog::SdlCallFailed, "SDL_InitSubSystem(SDL_INIT_AUDIO)", SDL_GetError());
                    return false;
                }

                // 呼ばれ始める前に入れておく(OnRequest が読む)。
                m_callback = callback;
                m_user = user;

                SDL_AudioSpec spec{};
                spec.format = SDL_AUDIO_F32;
                spec.channels = OutputChannels;
                spec.freq = OutputSampleRate;
                // 装置の形が違っても、SDL のストリームが変換してくれる(こちらはいつも同じ形で書けばよい)。
                m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &OnRequest, this);
                if (m_stream == nullptr)
                {
                    HUSKY_LOG(AudioLog::SdlCallFailed, "SDL_OpenAudioDeviceStream", SDL_GetError());
                    SDL_QuitSubSystem(SDL_INIT_AUDIO);
                    return false;
                }
                // 開いた直後は止まっている。動かすと、音のスレッドが OnRequest を呼び始める。
                if (!SDL_ResumeAudioStreamDevice(m_stream))
                {
                    HUSKY_LOG(AudioLog::SdlCallFailed, "SDL_ResumeAudioStreamDevice", SDL_GetError());
                    Stop();
                    return false;
                }
                return true;
            }

            void Stop() override
            {
                if (m_stream == nullptr)
                {
                    return;
                }
                // 壊すとき、SDL はストリームの鍵を取るので、OnRequest の最中ならそれが終わるまで待つ。
                // 戻った後は OnRequest は呼ばれない。
                SDL_DestroyAudioStream(m_stream);
                m_stream = nullptr;
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }

            const char* GetName() const override
            {
                const char* driver = SDL_GetCurrentAudioDriver();
                return driver != nullptr ? driver : "SDL";
            }

        private:
            // 1 回に書くフレーム数の上限(入れ物の大きさ)。足りない分は何回かに分けて書く。
            static constexpr int ChunkFrames = 1024;

            // SDL の音のスレッドから呼ばれる。additionalAmount は「あと何バイト欲しいか」。
            // 動作スレッド: SDL の音のスレッド
            static void SDLCALL OnRequest(void* userdata, SDL_AudioStream* stream, int additionalAmount, int /*totalAmount*/)
            {
                SdlAudioOutput& self = *static_cast<SdlAudioOutput*>(userdata);
                constexpr int bytesPerFrame = static_cast<int>(sizeof(float)) * OutputChannels;
                int frames = additionalAmount / bytesPerFrame;
                while (frames > 0)
                {
                    const int count = std::min(frames, ChunkFrames);
                    self.m_callback(self.m_user, self.m_buffer, count);
                    SDL_PutAudioStreamData(stream, self.m_buffer, count * bytesPerFrame);
                    frames -= count;
                }
            }

            SDL_AudioStream* m_stream = nullptr;
            RenderCallback m_callback = nullptr;
            void* m_user = nullptr;
            float m_buffer[ChunkFrames * OutputChannels] = {}; // 音のスレッドだけが使う(毎回確保しないよう持っておく)
        };
    }

    std::unique_ptr<AudioOutput> CreateSdlAudioOutput()
    {
        return std::make_unique<SdlAudioOutput>();
    }
}
