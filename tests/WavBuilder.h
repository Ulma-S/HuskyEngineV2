// テスト用に、WAV ファイルのバイト列を組み立てる。
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

namespace husky::tests
{
    inline void AppendU16(std::string& out, std::uint32_t value)
    {
        out.push_back(static_cast<char>(value & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
    }

    inline void AppendU32(std::string& out, std::uint32_t value)
    {
        AppendU16(out, value & 0xFFFF);
        AppendU16(out, value >> 16);
    }

    // format: 1 = 整数の PCM、3 = float。data は "data" チャンクの中身(バイト)。
    inline std::string MakeWavRaw(int format, int sampleRate, int channels, int bits, std::initializer_list<int> data)
    {
        std::string body = "WAVE";
        body += "fmt ";
        AppendU32(body, 16);
        AppendU16(body, static_cast<std::uint32_t>(format));
        AppendU16(body, static_cast<std::uint32_t>(channels));
        AppendU32(body, static_cast<std::uint32_t>(sampleRate));
        const int blockAlign = channels * bits / 8;
        AppendU32(body, static_cast<std::uint32_t>(sampleRate * blockAlign));
        AppendU16(body, static_cast<std::uint32_t>(blockAlign));
        AppendU16(body, static_cast<std::uint32_t>(bits));
        body += "data";
        AppendU32(body, static_cast<std::uint32_t>(data.size()));
        for (int byte : data)
        {
            body.push_back(static_cast<char>(byte & 0xFF));
        }
        std::string out = "RIFF";
        AppendU32(out, static_cast<std::uint32_t>(body.size()));
        return out + body;
    }

    // 16 ビットの PCM。samples はフレームごとに channels 個ずつ。
    inline std::string MakeWav16(int sampleRate, int channels, std::initializer_list<int> samples)
    {
        std::string data;
        for (int sample : samples)
        {
            AppendU16(data, static_cast<std::uint32_t>(sample) & 0xFFFF);
        }
        std::string out = MakeWavRaw(1, sampleRate, channels, 16, {});
        // MakeWavRaw は data チャンクが空なので、大きさを直して中身を足す
        out.replace(out.size() - 4, 4, std::string());
        AppendU32(out, static_cast<std::uint32_t>(data.size()));
        out += data;
        const std::uint32_t riffSize = static_cast<std::uint32_t>(out.size() - 8);
        std::string sizeBytes;
        AppendU32(sizeBytes, riffSize);
        out.replace(4, 4, sizeBytes);
        return out;
    }
}
