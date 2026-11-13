// 音を鳴らす窓口。ゲームスレッドから使う。
//
// 中身は3つ:
//   ミキサー(Mixer)      … 音のスレッドだけが触る。鳴っている音を混ぜる。
//   命令の行列           … ゲームスレッドが「鳴らす・止める・音量」を積み、音のスレッドが混ぜる前に取り出して当てはめる。
//   出す先(AudioOutput) … 音のスレッドから Render を呼ぶ。中身は SDL(差し替えられる)。
//
// ゲームスレッドとミキサーは直接話さない(ミキサーを触っている最中に、別のスレッドが書き換えると壊れるため)。
// 2つのスレッドで共有するのは、命令の行列と「鳴っているものの一覧」だけで、どちらも mutex で守る。
// 鍵を持つのは写す間だけで、混ぜる処理は鍵の外で行う(ゲームスレッドを待たせないため)。
//
// 出す先が無い間(テスト、CLI、装置が開けなかったとき)は、Play は null の番号を返して何もしない。
// 使い方:
//   audio::AudioDevice audio;
//   audio.StartOutput(audio::CreateSdlAudioOutput());
//   audio::PlaybackId id = audio.Play(sound, params); // sound のデータは鳴り終わるまで壊さない
//   audio.StopOutput();
#pragma once

#include "audio/AudioOutput.h"
#include "audio/Mixer.h"
#include "core/Handle.h"
#include "core/HandleTable.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace husky::audio
{
    // AudioDevice が持つ音のデータ(ミキサーの形)。外からは SoundHandle で指す。
    struct SoundBuffer
    {
        std::vector<float> samples; // OutputChannels 個ずつ並ぶ。作った後は書き換えない(音のスレッドが読む)
        bool releasing = false;     // ReleaseSound を呼んだ(音のスレッドが手放すのを待っている)
    };
    using SoundHandle = core::Handle<SoundBuffer>;

    class AudioDevice
    {
    public:
        static constexpr int MaxPendingCommands = 256; // 1 回の混ぜる処理までに積める命令の数

        AudioDevice() = default;
        ~AudioDevice();
        AudioDevice(const AudioDevice&) = delete;
        AudioDevice& operator=(const AudioDevice&) = delete;

        // 以下はすべて、動作スレッド: ゲームスレッド

        // 出す先を付けて鳴らし始める。開けなければ警告を出し、出す先なしのまま(音なしで続く)。
        // 戻り値: 開けたら true。
        bool StartOutput(std::unique_ptr<AudioOutput> output);
        // 出す先を止めて外す。鳴っていた音はすべて止まる。
        void StopOutput();
        bool HasOutput() const { return m_output != nullptr; }

        // 音のデータを預ける(ミキサーの形: OutputChannels・OutputSampleRate・float)。空なら null。
        // 出す先が無くても預けられる(後で開いたときに鳴らせる)。
        SoundHandle CreateSound(std::vector<float> samples);
        // 手放す。鳴っている音は止まる。メモリは、音のスレッドがもう読まないと分かってから(次の Update 以降に)捨てる。
        // 出す先が無ければ、その場で捨てる。
        void ReleaseSound(SoundHandle sound);
        // 預けたデータのフレーム数(無ければ 0)。
        int GetSoundFrameCount(SoundHandle sound) const;
        // 預かっているデータの数(手放し待ちも含む)。
        std::size_t GetSoundCount() const { return m_sounds.Size(); }

        // 毎フレーム呼ぶ。音のスレッドが手放したデータを捨てる。
        void Update();

        // 預けたデータを鳴らす。手放したもの・無いものなら null。
        PlaybackId Play(SoundHandle sound, const PlayParams& params);

        // 鳴らす。番号はここで振る(音のスレッドを待たずに返せる)。
        // 出す先が無い・データが空・行列がいっぱいなら null の番号。
        // sound のデータは、鳴り終わるか Stop するまで壊さない(AUD-3 で解放を遅らせる)。
        PlaybackId Play(const SoundData& sound, const PlayParams& params);
        void Stop(PlaybackId id);
        void StopAll();
        void SetPlaybackVolume(PlaybackId id, float volume);
        void SetBusVolume(Bus bus, float volume);
        void SetMasterVolume(float volume);
        // 一時停止する・解く。止めている間も命令(鳴らす・止める)は受け付け、解いたときに続きから鳴る。
        void SetPaused(bool paused);

        // 鳴っているか。まだ積んだだけ(音のスレッドが受け取る前)の Play も「鳴っている」に数える。
        // 最後まで鳴って止まったことが分かるのは、次に混ぜた後(数ミリ秒〜十数ミリ秒の遅れがある)。
        bool IsPlaying(PlaybackId id) const;

        // 出す先に渡したフレームの合計(確かめる用)。どのスレッドからでもよい。
        std::uint64_t GetRenderedFrameCount() const { return m_renderedFrames.load(std::memory_order_relaxed); }

    private:
        struct Command
        {
            enum class Kind : std::uint8_t
            {
                Play,
                Stop,
                StopAll,
                SetPlaybackVolume,
                SetBusVolume,
                SetMasterVolume,
                StopSound, // sound のデータを使っている音を止める(手放す前)
                SetPaused,
            };
            Kind kind = Kind::Stop;
            PlaybackId id;
            SoundData sound;
            PlayParams params;
            Bus bus = Bus::Effect;
            float volume = 1.0f;
            bool paused = false;
        };

        // 命令を積む。いっぱいなら捨てて false(警告は1回だけ)。出す先が無ければ積まずに false。
        // outSequence: 積めたら、この命令の通し番号(音のスレッドがここまで当てはめたかを比べる)。
        bool Push(const Command& command, std::uint64_t* outSequence = nullptr);
        void FreeReleasedSounds(std::uint64_t appliedSequence);

        // 出す先から呼ばれる(AudioOutput の RenderCallback)。
        // 動作スレッド: 音のスレッド
        static void RenderCallback(void* user, float* out, int frameCount);
        void Render(float* out, int frameCount);
        void ApplyCommand(const Command& command);

        std::unique_ptr<AudioOutput> m_output;

        // ゲームスレッドと音のスレッドで共有する(m_mutex で守る)。
        mutable std::mutex m_mutex;
        Command m_pending[MaxPendingCommands];
        int m_pendingCount = 0;
        std::uint64_t m_pushedSequence = 0; // 最後に積んだ命令の通し番号
        PlaybackId m_playing[Mixer::MaxVoices]; // 音のスレッドが混ぜた後に書く「鳴っているもの」
        int m_playingCount = 0;

        // 音のスレッドだけ(出す先が動いている間)。
        Mixer m_mixer;
        Command m_applying[MaxPendingCommands]; // m_pending を写して、鍵の外で当てはめる
        PlaybackId m_playingScratch[Mixer::MaxVoices];

        // ゲームスレッドだけ。
        std::uint32_t m_nextId = 1;
        bool m_queueFullWarned = false;
        core::HandleTable<SoundBuffer> m_sounds;
        // 手放し待ち。sequence は StopSound の命令の通し番号(0 なら、行列がいっぱいでまだ積めていない)。
        struct PendingRelease
        {
            SoundHandle sound;
            std::uint64_t sequence = 0;
        };
        std::vector<PendingRelease> m_pendingReleases;

        std::atomic<std::uint64_t> m_renderedFrames{0};
        std::atomic<std::uint64_t> m_appliedSequence{0}; // 音のスレッドが当てはめ終えた命令の通し番号
    };
}
