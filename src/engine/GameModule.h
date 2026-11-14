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

#include "platform/DynamicLibrary.h"

#include <cstddef>
#include <string>
#include <vector>

namespace husky::core
{
    class ObjectRegistry;
    class TypeInfo;
    class TypeRegistry;
}

namespace husky::engine
{
    class Application;
    class AssetLoader;

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

    // 今のゲームのモジュールを持つ係(HR-3)。Application が持つ(いちばん先に作り、いちばん後に壊れる位置に置くので、
    // ゲームの型のオブジェクトがすべて消えてから DLL が外れる)。
    // 付けるときに表の registerTypes で型を登録し、登録した型を覚える。外すとき(Detach)は、DLL の中を
    // 指したままのものを片付けてから外す:
    //   1. ゲームの型の ScriptableData をローダのキャッシュから外す(AssetLoader::ForgetScriptableData)
    //   2. 型を鍵にした控えを捨てる(Prefab::ClearDefaultPropertiesCache)
    //   3. 破棄の予約を確定し、GC を回す
    //   4. ゲームの型(か、その子)のオブジェクトが1つでも残っていれば断る(残ったまま外すと、外れた DLL の中の
    //      vtable やデストラクタを呼んで落ちる)。シーンのアクターを消すのは呼ぶ側の役目(HR-4)
    //   5. 型の登録を外し、DLL を外す
    // 決まり: ゲームの Object の型は、すべて registerTypes で登録する(登録していない型のオブジェクトは数えられない)。
    // 気を付けること: ゲームのコードが作ったエンジンの型のオブジェクト(AddComponent<SpriteComponent> など)も、
    // vtable はゲームの DLL の中のものを指すことがある(MSVC は使うモジュールごとに vtable を作る)。型では見分けられ
    // ないので、シーンのものは呼ぶ側が先に全部消す。
    // 動作スレッド: ゲームスレッド。
    class GameModuleHost
    {
    public:
        // DLL を読み、型を登録する。読めなければ false(何も付けない)。付いていたものは先に外しておくこと。
        bool LoadFromLibrary(const std::string& path, core::TypeRegistry& types);
        // 表をそのまま付ける(静的リンクの製品と、テスト)。型を登録する。
        void Attach(const GameModule& module, core::TypeRegistry& types);
        // 外す(上の 1〜5)。何も付いていなければ true。残ったオブジェクトがあれば false(何も外さない。理由はログ)。
        bool Detach(core::TypeRegistry& types, core::ObjectRegistry& registry, AssetLoader& loader);

        bool IsAttached() const { return m_attached; }
        bool IsFromLibrary() const { return m_library.IsLoaded(); }
        const GameModule& GetModule() const { return m_module; }
        const std::vector<const core::TypeInfo*>& GetTypes() const { return m_types; }

        // type がゲームの型(か、その子)か。
        bool IsGameType(const core::TypeInfo& type) const;
        // ゲームの型のオブジェクトの数(破棄を予約済みのものも含む)。firstType には最初に見つけたものの型を書く。
        std::size_t CountGameObjects(core::ObjectRegistry& registry, const core::TypeInfo** firstType = nullptr) const;

    private:
        void RegisterTypes(core::TypeRegistry& types);

        platform::DynamicLibrary m_library;
        GameModule m_module;
        std::vector<const core::TypeInfo*> m_types; // ゲームが登録した型(登録簿の中の型情報。DLL の中にある)
        bool m_attached = false;
    };
}

// ゲームの DLL が入口の関数を書き出すための印(関数の宣言の前に書く)。
//   __declspec(dllexport): MSVC の「この関数を DLL の外に見せる」印。
//   __attribute__((visibility("default"))): Clang / GCC で同じ意味の印(Mac 用)。
#if defined(_WIN32)
#define HUSKY_GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define HUSKY_GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
