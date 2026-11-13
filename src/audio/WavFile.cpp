// WAV ファイルを読む。説明は WavFile.h。
#include "audio/WavFile.h"

#include "audio/Mixer.h" // OutputChannels、OutputSampleRate

#include <cmath>
#include <cstring>

namespace husky::audio
{
    namespace
    {
        // 小さいほうのバイトが先(リトルエンディアン)の数を読む。CPU の並びに頼らず、1 バイトずつ組み立てる。
        std::uint32_t ReadU32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint16_t ReadU16(const std::uint8_t* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        bool IsTag(const std::uint8_t* p, const char* tag)
        {
            return std::memcmp(p, tag, 4) == 0;
        }

        constexpr std::uint16_t FormatPcm = 1;          // 整数
        constexpr std::uint16_t FormatFloat = 3;        // 浮動小数点
        constexpr std::uint16_t FormatExtensible = 0xFFFE; // 本当の形は、fmt の後ろの方(サブフォーマット)に書いてある

        // 1 つのサンプルを -1〜1 の float にする。
        float ReadSample(const std::uint8_t* p, std::uint16_t format, int bits)
        {
            if (format == FormatFloat)
            {
                float value;
                const std::uint32_t raw = ReadU32(p);
                std::memcpy(&value, &raw, sizeof(value)); // ビットの並びをそのまま float として読む
                return value;
            }
            switch (bits)
            {
            case 8:
                return (static_cast<float>(p[0]) - 128.0f) / 128.0f; // 8 ビットだけは符号なし(真ん中が 128)
            case 16:
                return static_cast<float>(static_cast<std::int16_t>(ReadU16(p))) / 32768.0f;
            case 24:
            {
                // 3 バイトを 32 ビットの上の方に詰めてから、符号付きとして読む(上のビットが符号になる)。
                const std::int32_t value =
                    static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) | (static_cast<std::uint32_t>(p[1]) << 16) |
                                              (static_cast<std::uint32_t>(p[2]) << 24));
                return static_cast<float>(value) / 2147483648.0f;
            }
            case 32:
                return static_cast<float>(static_cast<std::int32_t>(ReadU32(p))) / 2147483648.0f;
            default:
                return 0.0f;
            }
        }
    }

    bool DecodeWav(const std::uint8_t* data, std::size_t size, DecodedSound& outSound, const char*& outError)
    {
        outSound = DecodedSound{};
        if (data == nullptr || size < 12 || !IsTag(data, "RIFF") || !IsTag(data + 8, "WAVE"))
        {
            outError = "not a WAV file (no RIFF/WAVE header)";
            return false;
        }

        std::uint16_t format = 0;
        int channels = 0;
        int sampleRate = 0;
        int bits = 0;
        const std::uint8_t* sampleData = nullptr;
        std::size_t sampleBytes = 0;

        // チャンクを順に見る。
        std::size_t offset = 12;
        while (offset + 8 <= size)
        {
            const std::uint8_t* chunk = data + offset;
            const std::size_t chunkSize = ReadU32(chunk + 4);
            const std::uint8_t* body = chunk + 8;
            const std::size_t available = size - offset - 8;
            if (IsTag(chunk, "fmt "))
            {
                if (chunkSize < 16 || available < 16)
                {
                    outError = "fmt chunk is too small";
                    return false;
                }
                format = ReadU16(body);
                channels = ReadU16(body + 2);
                sampleRate = static_cast<int>(ReadU32(body + 4));
                bits = ReadU16(body + 14);
                if (format == FormatExtensible)
                {
                    if (chunkSize < 26 || available < 26)
                    {
                        outError = "fmt chunk (extensible) is too small";
                        return false;
                    }
                    format = ReadU16(body + 24); // サブフォーマットの GUID の先頭 2 バイトが、ふつうの形の番号
                }
            }
            else if (IsTag(chunk, "data"))
            {
                sampleData = body;
                // 書いてある大きさがファイルより大きい(途中で切れたファイル)ときは、ある分だけ読む。
                sampleBytes = chunkSize < available ? chunkSize : available;
            }
            // 次のチャンクへ。中身の大きさが奇数なら、1 バイトの詰め物がある。
            offset += 8 + chunkSize + (chunkSize & 1);
        }

        if (format == 0)
        {
            outError = "no fmt chunk";
            return false;
        }
        if (sampleData == nullptr)
        {
            outError = "no data chunk";
            return false;
        }
        if (format != FormatPcm && format != FormatFloat)
        {
            outError = "unsupported format (only PCM and 32-bit float)";
            return false;
        }
        if (format == FormatFloat ? bits != 32 : (bits != 8 && bits != 16 && bits != 24 && bits != 32))
        {
            outError = "unsupported bits per sample";
            return false;
        }
        if (channels != 1 && channels != 2)
        {
            outError = "unsupported channel count (only 1 or 2)";
            return false;
        }
        if (sampleRate <= 0)
        {
            outError = "invalid sample rate";
            return false;
        }

        const int bytesPerSample = bits / 8;
        const std::size_t frameCount = sampleBytes / static_cast<std::size_t>(bytesPerSample * channels);
        outSound.sampleRate = sampleRate;
        outSound.channels = channels;
        outSound.samples.resize(frameCount * static_cast<std::size_t>(channels));
        for (std::size_t i = 0; i < outSound.samples.size(); ++i)
        {
            outSound.samples[i] = ReadSample(sampleData + i * static_cast<std::size_t>(bytesPerSample), format, bits);
        }
        return true;
    }

    std::vector<float> ConvertToOutputFormat(const DecodedSound& sound)
    {
        const int inFrames = sound.GetFrameCount();
        if (inFrames <= 0 || sound.sampleRate <= 0)
        {
            return {};
        }
        // 出す長さ(フレーム数)= 元のフレーム数 × (出す 1 秒のフレーム数 / 元の 1 秒のフレーム数)。
        const std::int64_t outFrames64 = static_cast<std::int64_t>(inFrames) * OutputSampleRate / sound.sampleRate;
        const int outFrames = static_cast<int>(outFrames64 > 0 ? outFrames64 : 1);
        std::vector<float> out(static_cast<std::size_t>(outFrames) * OutputChannels);

        // 元の i 番目のフレームの、チャンネル c(モノラルなら c に関係なく 0)。
        auto sample = [&sound](int frame, int channel) {
            const int c = sound.channels == 1 ? 0 : channel;
            return sound.samples[static_cast<std::size_t>(frame * sound.channels + c)];
        };

        const double step = static_cast<double>(sound.sampleRate) / OutputSampleRate; // 出す 1 フレームで、元を何フレーム進むか
        for (int i = 0; i < outFrames; ++i)
        {
            const double position = i * step;
            const int i0 = static_cast<int>(position);
            const int i1 = i0 + 1 < inFrames ? i0 + 1 : inFrames - 1;
            const float t = static_cast<float>(position - i0); // 2 つの間のどこか(0〜1)
            for (int c = 0; c < OutputChannels; ++c)
            {
                const float a = sample(i0 < inFrames ? i0 : inFrames - 1, c);
                const float b = sample(i1, c);
                out[static_cast<std::size_t>(i * OutputChannels + c)] = a + (b - a) * t;
            }
        }
        return out;
    }
}
