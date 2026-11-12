// platform が使う外部のライブラリ(SDL)のメモリの確保を、エンジンの入口(core::MemoryTracker)に通す。
//
// SDL は、自分の中の確保に malloc / calloc / realloc / free の4つを使い、差し替え口(SDL_SetMemoryFunctions)を用意している。
// ここで MemoryTracker の関数に差し替えると、SDL が使うメモリも、毎秒のログやデバッグの窓の「ヒープ」に数えられる。
//
// 【大事】必ず、プログラムの一番最初(main の最初の行)に呼ぶこと。
//   差し替える前に SDL が確保したメモリを、差し替えた後の free(TrackedFree)で返すと、
//   TrackedFree は「自分が確保した見出し」が無いメモリを読んで、壊れてしまう。
//   SDL には「今いくつ確保しているか」を確かめる機能が無い(無効にしてビルドされている)ので、
//   呼ぶ順番は、呼ぶ側が守るしかない。テストの exe の main(tests/TestMain.cpp)でも、最初に呼ぶ。
//
// 動作スレッド: メインスレッド(ほかのスレッドが動き出す前に呼ぶ)
#pragma once

namespace husky::platform
{
    // SDL のメモリの確保を、MemoryTracker に通す。2回目以降は何もしない。
    // 戻り値: 差し替えられた(か、もう差し替えてあった)なら true。
    bool InstallMemoryHooks();

    // InstallMemoryHooks が済んでいるか。確認とテスト用。
    bool AreMemoryHooksInstalled();
}
