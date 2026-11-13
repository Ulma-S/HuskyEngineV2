// 音を混ぜる処理(ミキサー)。いくつもの音(ボイス)を足し合わせて、出す先に渡す1本の波にする。
//
// SDL を知らないただのコード。出す先(AUD-2 の AudioOutput)が「次の N フレームを書いて」と Mix を呼ぶ。
// 音の出ない環境(テスト)でも、Mix を直接呼んで結果の数を確かめられる。
//
// 言葉:
//   サンプル: 波の高さ1つ(float、-1〜1)。
//   フレーム: 同じ瞬間の全チャンネルのサンプルの組(ステレオなら 左, 右 の 2 つ)。
//   48kHz: 1 秒に 48000 フレーム。
//
// 音のデータはミキサーが持たず、借りるだけ(SoundData はポインタと長さ)。
// 鳴っている間、データを壊さないのは呼ぶ側の責任(AUD-3 で解放を遅らせる)。
#pragma once

#include <cstdint>

namespace husky::audio
{
    // 出す形。ミキサーに渡すデータも、この形に直してから渡す(AUD-3 のローダが直す)。
    constexpr int OutputChannels = 2;       // ステレオ(左, 右 の順に交互に並ぶ)
    constexpr int OutputSampleRate = 48000; // 1 秒あたりのフレーム数

    // 音のデータ(借りるだけ)。samples は frameCount * OutputChannels 個の float。
    struct SoundData
    {
        const float* samples = nullptr;
        int frameCount = 0;
    };

    // 音の種類。種類ごとに音量を決められる(設定画面の「効果音の音量」「BGM の音量」にあたる)。
    enum class Bus : std::uint8_t
    {
        Effect,
        Music,
        Count, // 種類の数(配列の大きさに使う)
    };

    // 鳴らした1回を指す番号。0 は「何も指さない」。
    // 番号を振るのは呼ぶ側(AUD-2 ではゲームスレッド)。ミキサーは別のスレッドで動くので、
    // ミキサーが番号を返す形だと、ゲームスレッドはすぐに番号を受け取れないため。
    // 指すのは「鳴らした1回」で、ミキサーの席(Voice)ではない。古い効果音を止めて席を使い回しても、番号は新しくなる。
    struct PlaybackId
    {
        std::uint32_t value = 0;

        bool IsNull() const { return value == 0; }
        bool operator==(const PlaybackId& other) const { return value == other.value; }
        bool operator!=(const PlaybackId& other) const { return value != other.value; }
    };

    // 鳴らすときの決め事。
    struct PlayParams
    {
        float volume = 1.0f; // この1回の音量(0 以上。1 でそのまま)
        bool loop = false;   // 最後まで行ったら最初に戻るか(BGM など)
        Bus bus = Bus::Effect;
    };

    // 動作スレッド: 1 つのスレッドだけから使う(どれか1つ。AUD-2 では音のスレッド)。中で確保しない。
    class Mixer
    {
    public:
        static constexpr int MaxVoices = 32; // 同時に鳴らせる数

        // 鳴らし始める。id は呼ぶ側が振った 0 以外の番号(鳴っているものと重ねない)。
        // ボイスがいっぱいなら、一番古い効果音(Bus::Effect)を止めて場所を空ける。
        // 戻り値: 鳴らせたら true(データが空、id が 0、効果音のボイスが1つも無くて空けられないときは false)。
        bool Play(PlaybackId id, const SoundData& sound, const PlayParams& params);

        // 止める。もう止まっている・知らない番号なら何もしない。
        void Stop(PlaybackId id);
        void StopAll();
        // samples のデータを使っている音をすべて止める(データを手放す前に呼ぶ)。
        void StopUsing(const float* samples);

        // 鳴っているか(最後まで行って止まったものは false)。
        bool IsPlaying(PlaybackId id) const;
        int GetPlayingCount() const;
        // 鳴っているものの番号を out に書く(最大 MaxVoices 個)。戻り値は書いた数。
        int GetPlayingIds(PlaybackId* out, int capacity) const;

        void SetPlaybackVolume(PlaybackId id, float volume);
        void SetBusVolume(Bus bus, float volume);
        void SetMasterVolume(float volume);

        // 次の frameCount フレームを out に書く(足すのではなく上書き。鳴っていなければ 0)。
        // out は frameCount * OutputChannels 個の float。
        // 結果は -1〜1 に切り詰める(大きな音が重なって範囲を超えると、出す先で割れた音になるため)。
        void Mix(float* out, int frameCount);

    private:
        // ボイス: 同時に鳴らせる音の1本ぶんの席。鳴らした1回(PlaybackId)が、鳴っている間だけ1つの席を使う。
        struct Voice
        {
            PlaybackId id;           // null なら空き
            SoundData sound;
            PlayParams params;
            int position = 0;     // 次に読むフレーム
            std::uint64_t order = 0; // 鳴らし始めた順(大きいほど新しい。古いものを止めるときに使う)
        };

        Voice* FindVoice(PlaybackId id);
        const Voice* FindVoice(PlaybackId id) const;
        Voice* FindFreeVoice();

        Voice m_voices[MaxVoices];
        float m_busVolumes[static_cast<int>(Bus::Count)] = {1.0f, 1.0f};
        float m_masterVolume = 1.0f;
        std::uint64_t m_nextOrder = 1;
    };
}
