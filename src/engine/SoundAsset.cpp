// SoundAsset.h の実装: 音のアセットと、その読み方(SoundAssetKind)。
#include "engine/SoundAsset.h"

#include "audio/WavFile.h"
#include "core/ObjectRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "platform/FileSystem.h"

#include <cstdint>
#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        // 音の読み込み中の記録。ジョブが書くのは samples、error、succeeded だけ。
        struct SoundLoad final : AssetLoad
        {
            // 1段目: ファイルを読み、WAV として開く。
            bool ReadSource() override
            {
                if (!platform::ReadTextFile(path.c_str(), bytes)) // 中身をそのままのバイトで読む(文字として直さない)
                {
                    error = "could not read the file";
                    return false;
                }
                return audio::DecodeWav(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), decoded, error);
            }

            // 2段目: ミキサーの形(float32・ステレオ・48kHz)に直す。
            bool Cook() override
            {
                samples = audio::ConvertToOutputFormat(decoded);
                bytes = {};   // もう要らない(メモリを早く返す)
                decoded = {};
                if (samples.empty())
                {
                    error = "the sound is empty";
                    return false;
                }
                return true;
            }

            std::string bytes;           // 1段目の途中: ファイルの中身
            audio::DecodedSound decoded; // 1段目の結果: WAV を開いた波
            std::vector<float> samples;  // 2段目の結果: ミキサーの形に直した波
            const char* error = nullptr; // 読めなかった理由(文字列の定数。Finish がログに出す)
        };
    }

    // 音の読み方。名前のある名前空間に置くのは、SoundAsset が friend にするため。
    class SoundAssetKind final : public AssetKind
    {
    public:
        const core::TypeInfo& GetAssetType() const override { return SoundAsset::StaticType(); }
        const char* GetDisplayName() const override { return "音"; }
        bool CanLoad(std::string_view relativePath) const override
        {
            // 読めるのは WAV だけ(audio::DecodeWav)
            return relativePath.size() >= 4 && relativePath.substr(relativePath.size() - 4) == ".wav";
        }

        Asset* CreateAsset(core::ObjectRegistry& registry) const override { return registry.Create<SoundAsset>(); }
        std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const override
        {
            (void)asset;
            return std::make_unique<SoundLoad>();
        }

        bool Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) const override
        {
            // 型は、このローダがこの種類で作った箱と記録なので、static_cast で子の型に変えてよい(RTTI は使わない)。
            SoundAsset& sound = static_cast<SoundAsset&>(asset);
            SoundLoad& result = static_cast<SoundLoad&>(load);
            if (!result.succeeded)
            {
                HUSKY_LOG(AssetLog::LoadWarning, GetDisplayName(), result.path,
                          result.error != nullptr ? result.error : "unknown error");
                return false;
            }
            sound.m_frameCount = static_cast<int>(result.samples.size()) / audio::OutputChannels;
            // 音の層に波を預ける(付いていなければ、長さだけを覚えて「読めた」にする)。
            if (audio::AudioDevice* audio = loader.GetAudioDevice())
            {
                sound.m_sound = audio->CreateSound(std::move(result.samples));
            }
            return true;
        }

        void Release(AssetLoader& loader, Asset& asset) const override
        {
            SoundAsset& sound = static_cast<SoundAsset&>(asset);
            audio::AudioDevice* audio = loader.GetAudioDevice();
            if (audio != nullptr && !sound.m_sound.IsNull())
            {
                audio->ReleaseSound(sound.m_sound); // 鳴っていれば止め、音のスレッドが手放してから捨てる
            }
        }
    };

    SoundAsset::~SoundAsset()
    {
        ReleaseLoadedData();
    }

    std::unique_ptr<AssetKind> CreateSoundAssetKind()
    {
        return std::make_unique<SoundAssetKind>();
    }
}
