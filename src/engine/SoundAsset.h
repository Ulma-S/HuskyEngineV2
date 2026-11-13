// 音のアセット。WAV ファイル1つ(と .meta)を読み込んだもので、GC 管理のオブジェクト(core::Object)。
// テクスチャ(TextureAsset)と同じ形: ファイル + .meta をそのままアセットにし、AssetRef<SoundAsset> で指す。
//
// 中身の波(ミキサーの形に直したもの)は音の層(audio::AudioDevice)が預かり、ここは番号(SoundHandle)だけを持つ。
// 鳴らすときは AudioDevice::Play(sound->GetSound(), params)。
//
// 寿命は GC が決める。回収されるとき、預けた波を手放す。鳴っている音は止まり、メモリは音のスレッドが
// もう読まないと分かってから捨てる(AudioDevice::ReleaseSound)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "audio/AudioDevice.h"
#include "core/Guid.h"
#include "core/Object.h"
#include "engine/AssetState.h"

namespace husky::engine
{
    class AssetLoader;

    class SoundAsset : public core::Object
    {
        HUSKY_OBJECT(SoundAsset, core::Object)
    public:
        // 回収されるとき、預けた波を手放すよう、ローダに伝える。
        ~SoundAsset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }

        // 長さ(ミキサーの形でのフレーム数と秒)。読み込めていなければ 0。
        int GetFrameCount() const { return m_frameCount; }
        float GetDurationSeconds() const { return static_cast<float>(m_frameCount) / audio::OutputSampleRate; }

        // 音の層に預けた波。ローダに音の層が付いていない(テストなど)ときは、何も指さない。
        audio::SoundHandle GetSound() const { return m_sound; }

    private:
        friend class AssetLoader; // 中身を書くのはローダだけ

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        int m_frameCount = 0;
        audio::SoundHandle m_sound;
        AssetLoader* m_loader = nullptr; // 作ったローダ(借りているだけ)
    };
}
