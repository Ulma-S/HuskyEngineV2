// 音を鳴らすコンポーネント(Unity の AudioSource にあたる)。アクターに付けて、音のアセットを鳴らす。
//
//   sound       … 鳴らす音(.wav のアセット)
//   volume      … この音の音量(0 以上。1 でそのまま)
//   loop        … 最後まで行ったら最初に戻るか(BGM など)
//   playOnStart … 再生を始めて最初の Tick で鳴らすか(BGM など)
//   bus         … 音の種類(Effect / Music)。種類ごとの音量が掛かる
//
// 音のアセットがまだ読み終わっていなければ、鳴らすのを読み終わるまで待つ(Tick で確かめる)。
// コンポーネントが壊れるとき、鳴らしている音を止める(BGM が鳴り続けないように)。
// 編集中は Tick が止まるので鳴らない。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "audio/AudioDevice.h"
#include "core/AssetRef.h"
#include "core/EnumReflection.h"
#include "engine/Component.h"
#include "engine/SoundAsset.h"

namespace husky::audio
{
    // audio::Bus をプロパティにできるよう、名前の表を登録する(保存は名前)。
    // 音の層はリフレクションを知らないので、使う側の engine で登録する(enum と同じ名前空間に書く決まり)。
    HUSKY_ENUM_DECLARE(Bus) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)
}

namespace husky::engine
{
    class Scene;

    class AudioSourceComponent : public Component
    {
        HUSKY_OBJECT(AudioSourceComponent, Component)
    public:
        ~AudioSourceComponent() override;

        core::AssetRef<SoundAsset> sound;
        float volume = 1.0f;
        bool loop = false;
        bool playOnStart = false;
        audio::Bus bus = audio::Bus::Effect;

        // 鳴らす(鳴っていれば、止めてから最初から)。読み終わっていなければ、読み終わったときに鳴らす。
        void Play();
        void Stop();
        // 鳴っているか(読み終わるのを待っている間も true)。
        bool IsPlaying() const;

        void Tick(double dt) override;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(sound)
            HUSKY_PROPERTY(volume)
            HUSKY_PROPERTY(loop)
            HUSKY_PROPERTY(playOnStart)
            HUSKY_PROPERTY(bus)
            HUSKY_METHOD(Play) // UI のボタンなどから名前で呼べるように(UI-6b)
            HUSKY_METHOD(Stop)
        HUSKY_REFLECTION_END()

    private:
        // 読み終わっていれば鳴らして true。まだなら false(次の Tick でもう一度)。読めなかったらあきらめて true。
        bool TryStartPlayback();

        // 保存しない(その場の状態)。
        audio::AudioDevice* m_audio = nullptr; // 鳴らした窓口(借りるだけ。壊れるときに止めるため)
        audio::PlaybackId m_playback;
        bool m_started = false;       // 最初の Tick を過ぎたか(playOnStart 用)
        bool m_playRequested = false; // 鳴らしたいが、音がまだ読み終わっていない
    };

    // 1 回だけ鳴らす(コンポーネントを使わない、効果音向け)。scene のローダで sound を引き、読み終わっていれば鳴らす。
    // 読み終わっていなければ鳴らさずに読み始めるだけ(効果音は遅れて鳴るより鳴らないほうがよいため)。
    // sound を持つもの(プロパティの AssetRef)が、鳴り終わるまで生きていること(アセットが回収されると音は止まる)。
    // 戻り値: 鳴らした番号(鳴らせなければ null)。
    audio::PlaybackId PlaySoundOnce(Scene& scene, core::AssetRef<SoundAsset>& sound, float volume = 1.0f,
                                    audio::Bus bus = audio::Bus::Effect);
}
