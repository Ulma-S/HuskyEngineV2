// 音を混ぜる処理。説明は Mixer.h。
#include "audio/Mixer.h"

#include <algorithm>

namespace husky::audio
{
    namespace
    {
        // 音量は 0 より下にしない(マイナスにすると波が上下逆になるだけで、小さくならない)。
        float ClampVolume(float volume)
        {
            return volume < 0.0f ? 0.0f : volume;
        }
    }

    bool Mixer::Play(VoiceId id, const SoundData& sound, const PlayParams& params)
    {
        if (id.IsNull() || sound.samples == nullptr || sound.frameCount <= 0)
        {
            return false;
        }

        Voice* voice = FindFreeVoice();
        if (voice == nullptr)
        {
            // いっぱい: 一番古い効果音を止めて使う。BGM(ループするものが多い)は止めない。
            for (Voice& candidate : m_voices)
            {
                if (candidate.params.bus == Bus::Effect && (voice == nullptr || candidate.order < voice->order))
                {
                    voice = &candidate;
                }
            }
            if (voice == nullptr)
            {
                return false;
            }
        }

        voice->id = id;
        voice->sound = sound;
        voice->params = params;
        voice->params.volume = ClampVolume(params.volume);
        voice->position = 0;
        voice->order = m_nextOrder++;
        return true;
    }

    void Mixer::Stop(VoiceId id)
    {
        if (Voice* voice = FindVoice(id))
        {
            *voice = Voice{};
        }
    }

    void Mixer::StopAll()
    {
        for (Voice& voice : m_voices)
        {
            voice = Voice{};
        }
    }

    bool Mixer::IsPlaying(VoiceId id) const
    {
        return FindVoice(id) != nullptr;
    }

    int Mixer::GetPlayingCount() const
    {
        int count = 0;
        for (const Voice& voice : m_voices)
        {
            if (!voice.id.IsNull())
            {
                ++count;
            }
        }
        return count;
    }

    void Mixer::SetVoiceVolume(VoiceId id, float volume)
    {
        if (Voice* voice = FindVoice(id))
        {
            voice->params.volume = ClampVolume(volume);
        }
    }

    void Mixer::SetBusVolume(Bus bus, float volume)
    {
        const int index = static_cast<int>(bus);
        if (index >= 0 && index < static_cast<int>(Bus::Count))
        {
            m_busVolumes[index] = ClampVolume(volume);
        }
    }

    void Mixer::SetMasterVolume(float volume)
    {
        m_masterVolume = ClampVolume(volume);
    }

    void Mixer::Mix(float* out, int frameCount)
    {
        if (out == nullptr || frameCount <= 0)
        {
            return;
        }
        const int sampleCount = frameCount * OutputChannels;
        std::fill(out, out + sampleCount, 0.0f);

        for (Voice& voice : m_voices)
        {
            if (voice.id.IsNull())
            {
                continue;
            }
            // 掛ける音量は、全体 × 種類 × この1回。フレームごとではなく、ボイスごとに1回だけ計算する。
            const float gain = m_masterVolume * m_busVolumes[static_cast<int>(voice.params.bus)] * voice.params.volume;

            int written = 0; // out に書いたフレーム数
            while (written < frameCount)
            {
                // データの残りと out の残りの、短いほうだけ一気に足す。
                const int remaining = voice.sound.frameCount - voice.position;
                const int count = std::min(remaining, frameCount - written);
                const float* source = voice.sound.samples + voice.position * OutputChannels;
                float* destination = out + written * OutputChannels;
                for (int i = 0; i < count * OutputChannels; ++i)
                {
                    destination[i] += source[i] * gain;
                }
                written += count;
                voice.position += count;

                if (voice.position >= voice.sound.frameCount)
                {
                    if (!voice.params.loop)
                    {
                        voice = Voice{}; // 最後まで鳴った: 空きに戻す
                        break;
                    }
                    voice.position = 0; // ループ: 最初へ戻って、out の残りを続けて埋める
                }
            }
        }

        // 重なって範囲を超えた所を切り詰める。
        for (int i = 0; i < sampleCount; ++i)
        {
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
        }
    }

    Mixer::Voice* Mixer::FindVoice(VoiceId id)
    {
        if (id.IsNull())
        {
            return nullptr;
        }
        for (Voice& voice : m_voices)
        {
            if (voice.id == id)
            {
                return &voice;
            }
        }
        return nullptr;
    }

    const Mixer::Voice* Mixer::FindVoice(VoiceId id) const
    {
        return const_cast<Mixer*>(this)->FindVoice(id);
    }

    Mixer::Voice* Mixer::FindFreeVoice()
    {
        for (Voice& voice : m_voices)
        {
            if (voice.id.IsNull())
            {
                return &voice;
            }
        }
        return nullptr;
    }
}
