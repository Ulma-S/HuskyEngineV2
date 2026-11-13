// 音の出す先(スピーカーなど)の窓口。
//
// 出す先は、自分のスレッド(音のスレッド)から「次の N フレームを書いて」と RenderCallback を呼ぶだけ。
// 何を鳴らすか(混ぜる処理)は知らない。中身を替えれば、SDL 以外(OS の API を直接、別のライブラリ)にも替えられる。
// 今の中身は SDL だけ(SdlAudioOutput.cpp。SDL を触るのは audio の中でこのファイルだけ)。
#pragma once

#include <memory>

namespace husky::audio
{
    // 音のスレッドから呼ばれる。out に frameCount フレーム(audio::OutputChannels チャンネル、float)を書く。
    // user は Start に渡したもの。
    using RenderCallback = void (*)(void* user, float* out, int frameCount);

    class AudioOutput
    {
    public:
        virtual ~AudioOutput() = default; // 子の型を、親のポインタ(unique_ptr<AudioOutput>)で壊すため

        // 出す先を開いて、callback を呼び始める。形は audio::OutputChannels・OutputSampleRate・float。
        // 戻り値: 開けたら true。
        // 動作スレッド: ゲームスレッド
        virtual bool Start(RenderCallback callback, void* user) = 0;

        // 止める。戻ったら、もう callback は呼ばれない(呼んでいる最中なら終わるまで待つ)。
        // 動作スレッド: ゲームスレッド
        virtual void Stop() = 0;

        // ログに出す名前(SDL のドライバの名前など)。
        virtual const char* GetName() const = 0;
    };

    // SDL3 の音の装置(既定の再生の装置)に出す中身を作る。
    std::unique_ptr<AudioOutput> CreateSdlAudioOutput();
}
