// 音を鳴らす窓口。説明は AudioDevice.h。
#include "audio/AudioDevice.h"

#include "audio/AudioLog.h"

#include <algorithm>

namespace husky::audio
{
    AudioDevice::~AudioDevice()
    {
        StopOutput();
    }

    bool AudioDevice::StartOutput(std::unique_ptr<AudioOutput> output)
    {
        StopOutput();
        if (output == nullptr)
        {
            return false;
        }
        // 先に m_output を入れる(Start の直後から Render が呼ばれうるが、Render は m_output を触らないので順番は問わない)。
        m_output = std::move(output);
        if (!m_output->Start(&AudioDevice::RenderCallback, this))
        {
            m_output.reset();
            HUSKY_LOG(AudioLog::OutputUnavailable);
            return false;
        }
        HUSKY_LOG(AudioLog::OutputStarted, m_output->GetName(), OutputSampleRate, OutputChannels);
        return true;
    }

    void AudioDevice::StopOutput()
    {
        if (m_output == nullptr)
        {
            return;
        }
        // Stop が戻った後は、音のスレッドは Render を呼ばない。ここからはミキサーもゲームスレッドで触ってよい。
        m_output->Stop();
        m_output.reset();
        m_mixer.StopAll();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingCount = 0;
            m_playingCount = 0;
        }
        HUSKY_LOG(AudioLog::OutputStopped, static_cast<std::int64_t>(GetRenderedFrameCount()));
    }

    PlaybackId AudioDevice::Play(const SoundData& sound, const PlayParams& params)
    {
        if (sound.samples == nullptr || sound.frameCount <= 0)
        {
            return PlaybackId{};
        }
        Command command;
        command.kind = Command::Kind::Play;
        command.id = PlaybackId{m_nextId};
        command.sound = sound;
        command.params = params;
        if (!Push(command))
        {
            return PlaybackId{};
        }
        // 0 は「何も指さない」なので飛ばす(42 億回で一周する。そのころ同じ番号の音はもう鳴っていない)。
        ++m_nextId;
        if (m_nextId == 0)
        {
            m_nextId = 1;
        }
        return command.id;
    }

    void AudioDevice::Stop(PlaybackId id)
    {
        if (id.IsNull())
        {
            return;
        }
        Command command;
        command.kind = Command::Kind::Stop;
        command.id = id;
        Push(command);
    }

    void AudioDevice::StopAll()
    {
        Command command;
        command.kind = Command::Kind::StopAll;
        Push(command);
    }

    void AudioDevice::SetPlaybackVolume(PlaybackId id, float volume)
    {
        Command command;
        command.kind = Command::Kind::SetPlaybackVolume;
        command.id = id;
        command.volume = volume;
        Push(command);
    }

    void AudioDevice::SetBusVolume(Bus bus, float volume)
    {
        if (m_output == nullptr)
        {
            m_mixer.SetBusVolume(bus, volume); // 音のスレッドが無いので直接でよい(次に開いたときもそのまま効く)
            return;
        }
        Command command;
        command.kind = Command::Kind::SetBusVolume;
        command.bus = bus;
        command.volume = volume;
        Push(command);
    }

    void AudioDevice::SetMasterVolume(float volume)
    {
        if (m_output == nullptr)
        {
            m_mixer.SetMasterVolume(volume);
            return;
        }
        Command command;
        command.kind = Command::Kind::SetMasterVolume;
        command.volume = volume;
        Push(command);
    }

    bool AudioDevice::IsPlaying(PlaybackId id) const
    {
        if (id.IsNull())
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        // まず、最後に混ぜたときに鳴っていたか。
        bool playing = std::find(m_playing, m_playing + m_playingCount, id) != m_playing + m_playingCount;
        // そこへ、まだ当てはめていない命令を順に重ねる。
        for (int i = 0; i < m_pendingCount; ++i)
        {
            const Command& command = m_pending[i];
            if (command.kind == Command::Kind::Play && command.id == id)
            {
                playing = true;
            }
            else if ((command.kind == Command::Kind::Stop && command.id == id) || command.kind == Command::Kind::StopAll)
            {
                playing = false;
            }
        }
        return playing;
    }

    bool AudioDevice::Push(const Command& command)
    {
        if (m_output == nullptr)
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pendingCount < MaxPendingCommands)
            {
                m_pending[m_pendingCount++] = command;
                return true;
            }
        }
        // いっぱい: 音のスレッドが止まっているか、1 フレームに命令を積みすぎ。ログは鍵の外で、1回だけ出す。
        if (!m_queueFullWarned)
        {
            m_queueFullWarned = true;
            HUSKY_LOG(AudioLog::CommandQueueFull, MaxPendingCommands);
        }
        return false;
    }

    void AudioDevice::RenderCallback(void* user, float* out, int frameCount)
    {
        static_cast<AudioDevice*>(user)->Render(out, frameCount);
    }

    void AudioDevice::Render(float* out, int frameCount)
    {
        // 1. 積まれた命令を手元へ写す(鍵を持つのは写す間だけ)。
        int commandCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            commandCount = m_pendingCount;
            std::copy(m_pending, m_pending + commandCount, m_applying);
            m_pendingCount = 0;
        }

        // 2. 鍵の外で当てはめて、混ぜる。
        for (int i = 0; i < commandCount; ++i)
        {
            ApplyCommand(m_applying[i]);
        }
        m_mixer.Mix(out, frameCount);

        // 3. 鳴っているものの一覧を、ゲームスレッドの IsPlaying 用に書く。
        const int playingCount = m_mixer.GetPlayingIds(m_playingScratch, Mixer::MaxVoices);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::copy(m_playingScratch, m_playingScratch + playingCount, m_playing);
            m_playingCount = playingCount;
        }
        m_renderedFrames.fetch_add(static_cast<std::uint64_t>(frameCount), std::memory_order_relaxed);
    }

    void AudioDevice::ApplyCommand(const Command& command)
    {
        switch (command.kind)
        {
        case Command::Kind::Play:
            m_mixer.Play(command.id, command.sound, command.params);
            break;
        case Command::Kind::Stop:
            m_mixer.Stop(command.id);
            break;
        case Command::Kind::StopAll:
            m_mixer.StopAll();
            break;
        case Command::Kind::SetPlaybackVolume:
            m_mixer.SetPlaybackVolume(command.id, command.volume);
            break;
        case Command::Kind::SetBusVolume:
            m_mixer.SetBusVolume(command.bus, command.volume);
            break;
        case Command::Kind::SetMasterVolume:
            m_mixer.SetMasterVolume(command.volume);
            break;
        }
    }
}
