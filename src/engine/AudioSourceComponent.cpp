// 音を鳴らすコンポーネント。説明は AudioSourceComponent.h。
#include "engine/AudioSourceComponent.h"

#include "engine/Actor.h"
#include "engine/AssetLoader.h"
#include "engine/World.h"

namespace husky::engine
{
    AudioSourceComponent::~AudioSourceComponent()
    {
        // 窓口(Application の中)はレジストリより長く生きるので、ここで呼んでよい。
        if (m_audio != nullptr && !m_playback.IsNull())
        {
            m_audio->Stop(m_playback);
        }
    }

    void AudioSourceComponent::Play()
    {
        Stop();
        m_playRequested = true;
        if (TryStartPlayback())
        {
            m_playRequested = false;
        }
    }

    void AudioSourceComponent::Stop()
    {
        m_playRequested = false;
        if (m_audio != nullptr && !m_playback.IsNull())
        {
            m_audio->Stop(m_playback);
        }
        m_playback = audio::PlaybackId{};
    }

    bool AudioSourceComponent::IsPlaying() const
    {
        if (m_playRequested)
        {
            return true;
        }
        return m_audio != nullptr && m_audio->IsPlaying(m_playback);
    }

    void AudioSourceComponent::Tick(double dt)
    {
        (void)dt;
        if (!m_started)
        {
            m_started = true;
            if (playOnStart)
            {
                Play();
            }
        }
        if (m_playRequested && TryStartPlayback())
        {
            m_playRequested = false;
        }
    }

    bool AudioSourceComponent::TryStartPlayback()
    {
        Actor* owner = GetOwner();
        World* scene = owner != nullptr ? owner->GetWorld() : nullptr;
        if (scene == nullptr || scene->GetAudio() == nullptr || scene->GetAssetLoader() == nullptr)
        {
            return true; // 鳴らす先が無い(テストなど): あきらめる
        }
        const SoundAsset* asset = scene->GetAssetLoader()->Resolve(sound);
        if (asset == nullptr || asset->GetState() == AssetState::Failed)
        {
            return true; // 何も指していない・読めない: あきらめる(読めない理由はローダがログに出す)
        }
        if (!asset->IsLoaded())
        {
            return false; // 読み込み中: 次の Tick でもう一度
        }
        audio::PlayParams params;
        params.volume = volume;
        params.loop = loop;
        params.bus = bus;
        m_audio = scene->GetAudio();
        m_playback = m_audio->Play(asset->GetSound(), params);
        return true;
    }

    audio::PlaybackId PlaySoundOnce(World& scene, core::AssetRef<SoundAsset>& sound, float volume, audio::Bus bus)
    {
        if (scene.GetAudio() == nullptr || scene.GetAssetLoader() == nullptr)
        {
            return audio::PlaybackId{};
        }
        const SoundAsset* asset = scene.GetAssetLoader()->Resolve(sound);
        if (asset == nullptr || !asset->IsLoaded())
        {
            return audio::PlaybackId{};
        }
        audio::PlayParams params;
        params.volume = volume;
        params.bus = bus;
        return scene.GetAudio()->Play(asset->GetSound(), params);
    }
}
