// WAV ファイルを読み、ミキサーの形(float32・ステレオ・48kHz)に直す。
//
// SDL を使わない自前のコード(SDL への依存を後から切り離せるように。WAV は形が簡単なので自分で読める)。
// どのスレッドでも動く(ローダのジョブから呼ぶ)。GC 管理のオブジェクトは触らない。
//
// WAV の形(RIFF): 先頭 "RIFF" + 大きさ + "WAVE" の後に、チャンク(4 文字の名前 + 大きさ + 中身)が並ぶ。
//   "fmt " … 形(PCM か float か、チャンネル数、1 秒のフレーム数、1 サンプルのビット数)
//   "data" … 波そのもの(フレームごとに、チャンネルの数だけサンプルが並ぶ)
//   ほかのチャンク(LIST など)は読み飛ばす。数は小さいほうが先(リトルエンディアン)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace husky::audio
{
    // 読んだままの波(形はファイルのまま。サンプルだけ float の -1〜1 に直してある)。
    struct DecodedSound
    {
        int sampleRate = 0; // 1 秒あたりのフレーム数
        int channels = 0;   // 1(モノラル)か 2(ステレオ)
        std::vector<float> samples; // フレームごとに channels 個ずつ並ぶ
        int GetFrameCount() const { return channels > 0 ? static_cast<int>(samples.size()) / channels : 0; }
    };

    // 読める形: 整数の PCM(8 / 16 / 24 / 32 ビット)と float(32 ビット)、1 か 2 チャンネル。
    // 戻り値: 読めたら true。読めなければ false で、outError に理由(文字列の定数)を入れる。
    bool DecodeWav(const std::uint8_t* data, std::size_t size, DecodedSound& outSound, const char*& outError);

    // ミキサーの形(OutputChannels・OutputSampleRate)に直す。
    // モノラルは左右に同じ値を入れる。1 秒のフレーム数が違えば、隣り合う2つのサンプルの間を直線で結んで作る(線形補間)。
    std::vector<float> ConvertToOutputFormat(const DecodedSound& sound);
}
