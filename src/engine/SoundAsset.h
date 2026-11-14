// 音のアセット。WAV ファイル1つ(と .meta)を読み込んだもので、GC 管理のオブジェクト(Asset の子)。
// テクスチャ(TextureAsset)と同じ形: ファイル + .meta をそのままアセットにし、AssetRef<SoundAsset> で指す。
//
// 中身の波(ミキサーの形に直したもの)は音の層(audio::AudioDevice)が預かり、ここは番号(SoundHandle)だけを持つ。
// 鳴らすときは AudioDevice::Play(sound->GetSound(), params)。
//
// 読み方(SoundAssetKind、SoundAsset.cpp)はローダに登録する(AL-1。Application が CreateSoundAssetKind で登録)。
// 寿命は GC が決める。回収されるとき、預けた波を手放す。鳴っている音は止まり、メモリは音のスレッドが
// もう読まないと分かってから捨てる(AudioDevice::ReleaseSound)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "audio/AudioDevice.h"
#include "engine/Asset.h"

#include <memory>

namespace husky::engine
{
    class SoundAssetKind;

    class SoundAsset : public Asset
    {
        HUSKY_OBJECT(SoundAsset, Asset)
    public:
        // 回収されるとき、預けた波を手放す(SoundAssetKind::Release)。
        ~SoundAsset() override;

        // 長さ(ミキサーの形でのフレーム数と秒)。読み込めていなければ 0。
        int GetFrameCount() const { return m_frameCount; }
        float GetDurationSeconds() const { return static_cast<float>(m_frameCount) / audio::OutputSampleRate; }

        // 音の層に預けた波。ローダに音の層が付いていない(テストなど)ときは、何も指さない。
        audio::SoundHandle GetSound() const { return m_sound; }

    private:
        friend class SoundAssetKind; // 中身を書くのは読み方だけ

        int m_frameCount = 0;
        audio::SoundHandle m_sound;
    };

    // 音(.wav)の読み方を作る。ローダに RegisterKind で登録する。
    std::unique_ptr<AssetKind> CreateSoundAssetKind();
}
