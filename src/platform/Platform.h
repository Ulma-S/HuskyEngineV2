// platform モジュールの全体の初期化・終了と、OS からのイベント処理。
// このヘッダには SDL の型を一切書かない(SDL を触ってよいのは platform の .cpp だけ)。
#pragma once // このヘッダが同じ .cpp の中で2回読み込まれても、2回目は無視させる

#include "platform/InputEvents.h"

#include <cstdint> // std::uint64_t など、ビット数が決まった整数型
#include <vector>  // std::vector(C# の List<T> にあたる、伸び縮みする配列)

namespace husky::platform
{
    // platform 全体を初期化する。ほかの platform の機能を使う前に、1回だけ呼ぶ。
    // 戻り値: 成功したら true。失敗の理由はログに出す。
    // 動作スレッド: メインスレッド
    bool Initialize();

    // platform 全体を終了する。Initialize が成功したときだけ、最後に1回呼ぶ。
    // Window などの platform のオブジェクトは、先に全部破棄しておくこと。
    // 動作スレッド: メインスレッド
    void Shutdown();

    // コンソール(コマンドプロンプトなど)に出す文字を UTF-8 として表示させる。エンジンのログは UTF-8 の日本語なので、
    // Windows の既定(日本語版では Shift_JIS)のままだと文字化けする。コンソールで使う exe(エディタの CLI)の最初に呼ぶ。
    // Windows 以外では、もともと UTF-8 なので何もしない。Initialize の前に呼んでよい。
    // 動作スレッド: メインスレッド
    void EnableUtf8ConsoleOutput();

    // SDL 自身のログ(SDL の中の警告など)を、エンジンのログ(core/Log.h)の "Engine.Platform.SDL" チャンネルへ流す(ID の無いログ)。
    // Initialize が呼ぶ。Initialize を呼ばない exe(エディタの CLI)は自分で呼ぶ。何回呼んでもよい。
    // 動作スレッド: どこでも(SDL はどのスレッドからでもログを出すことがあり、受けたスレッドで流す)
    void ForwardSdlLogToEngine();

    // 開発用の UI が、キーボード・マウスを使っているか。製品では開発用の UI が無いので、いつも両方 false
    // (ProcessEvents の中で同じコードを使えるように、型は製品にも置く)。
    struct NativeEventCapture
    {
        bool keyboard = false; // ImGui の WantCaptureKeyboard
        bool mouse = false;    // ImGui の WantCaptureMouse(窓の上にマウスがある、窓をドラッグしているなど)
    };

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】OS のイベントを、開発用の UI(ImGui)にも渡すための口。描画層(render)が登録する。
    // platform は ImGui を知らない。登録された関数に、イベントを1つずつ渡すだけ。
    //   nativeEvent: 中身は SDL_Event*(ヘッダに SDL の型を出さないため void* にしている)
    //   戻り値: UI がキーボード・マウスを使っているか(NativeEventCapture)。使っている間は、
    //           キーやボタンを押した入力とホイールはゲームに渡さない。離した入力とマウスの位置は渡す
    //           (UI の上で離したせいで、ゲームの中で押したままの状態が残らないようにするため)
    // nullptr を渡すと外せる。
    using NativeEventHook = NativeEventCapture (*)(const void* nativeEvent);
    void SetNativeEventHook(NativeEventHook hook);
#endif

    // OS から届いたイベント(閉じるボタン、キー入力、マウスなど)を、溜まっている分すべて処理する。
    // 毎フレーム1回呼ぶ。
    // outEvents: このフレームに起きたキーとマウスのイベントを、起きた順に入れて返す(最初に keys と mouse を空にする。
    //   画面の大きさは触らない)。
    //   & は参照渡し。C++ では vector も渡すと丸ごとコピーされるため、呼ぶ側の入れ物を直接書き換えるのに使う。
    // 戻り値: アプリの終了が要求されたら true。
    // 動作スレッド: メインスレッド
    bool ProcessEvents(InputEvents& outEvents);

    // 起動してからの経過時間を、ナノ秒(10億分の1秒)の整数で返す。
    // 絶対値に意味はなく、2回呼んだ差を経過時間として使う。
    // 動作スレッド: どこでもよい
    std::uint64_t GetTimeNanoseconds();
}
