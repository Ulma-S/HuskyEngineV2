// ゲームのコード(モジュール)とエンジンの約束(HR-2)。
//
// ゲームのコードは、開発ビルドでは別の DLL(HuskyGame.dll)になり、exe が起動してから読み込む(後で読み直す。HR-4)。
// exe は DLL の中の C++ の関数を直接は呼べない(リンクしていない)ので、DLL は名前の決まった入口の関数
// HuskyGetGameModule を1つだけ書き出す。exe はそれを名前で探して呼び、ゲームの関数のポインタの表(GameModule)を受け取る。
// 表を通せば、DLL でも静的リンク(製品)でも、exe は同じ書き方でゲームを呼べる。
//
//   - extern "C": C++ は関数の名前に引数の型などを足して崩す(マングリング。?Foo@@YAXH@Z のような名前になる)。
//     extern "C" の関数は崩さず "HuskyGetGameModule" のまま書き出すので、名前で探せる。
//   - version: 表の形(GameModuleVersion)を変えたら数を上げる。古い DLL を読んだら、呼ぶ前に気づける。
//   - DLL と exe の間では、C++ の参照や std の型(Application&、TypeRegistry&)もそのまま渡す。
//     同じコンパイラ・同じ設定(Debug / Release、/MD)でビルドする前提なので、中の作りがそろっていて安全。
//
// 動作スレッド: ゲームスレッド。
#pragma once

#include <string>

namespace husky::core
{
    class TypeRegistry;
}

namespace husky::platform
{
    class DynamicLibrary;
}

namespace husky::engine
{
    class Application;

    // 表の形の版。GameModule のメンバーを変えたら上げる。
    constexpr int GameModuleVersion = 1;

    // ゲームが用意する関数の表。どれも nullptr でもよい(何もしない)。
    struct GameModule
    {
        int version = 0;
        // ゲームの型を登録する(シーンのファイルの "type" から作れるように)。
        void (*registerTypes)(core::TypeRegistry& types) = nullptr;
        // シーンのファイルが無いときに、最初のアクターを置く。
        void (*spawnInitialActors)(Application& app) = nullptr;
        // シーンを用意した後に呼ぶ(今のゲームは、UI が無ければ HUD を置く)。
        void (*onSceneReady)(Application& app) = nullptr;
    };

    // DLL が書き出す入口の関数の名前と型。out に表を書いて true を返す。
    constexpr const char* GameModuleEntryName = "HuskyGetGameModule";
    using GameModuleEntryFunction = bool (*)(GameModule* out);

    // ライブラリを読み、入口を呼んで表を受け取る。読めない・入口が無い・版が違うときは false(理由はログ)。
    // 表の関数はライブラリの中を指すので、library を外したら使えない。
    bool LoadGameModule(platform::DynamicLibrary& library, const std::string& path, GameModule& out);
}

// ゲームの DLL が入口の関数を書き出すための印(関数の宣言の前に書く)。
//   __declspec(dllexport): MSVC の「この関数を DLL の外に見せる」印。
//   __attribute__((visibility("default"))): Clang / GCC で同じ意味の印(Mac 用)。
#if defined(_WIN32)
#define HUSKY_GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define HUSKY_GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
