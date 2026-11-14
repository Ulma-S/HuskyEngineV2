// 実行中に読み込むライブラリ(Windows の DLL、Mac の .dylib)の窓口(HR-2)。
//
// ゲームのコード(HuskyGame.dll)を exe に組み込まず、起動してから読み込むために使う。
// 中身は SDL の SDL_LoadObject / SDL_LoadFunction / SDL_UnloadObject(Windows では LoadLibrary、Mac では dlopen)。
// SDL を触るのは platform の中だけなので、ここで SDL の型を外に出さない(持つのは void* だけ)。
//
// 使い方:
//   DynamicLibrary library;
//   if (library.Load("HuskyGame.dll")) { auto* f = reinterpret_cast<Fn>(library.FindFunction("Name")); }
//   // library が壊れるとき(か Unload)にライブラリを外す。外した後は、その中の関数・データ・型(vtable)を使えない。
//
// 動作スレッド: ゲームスレッド。
#pragma once

#include <string>

namespace husky::platform
{
    class DynamicLibrary
    {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        // 持ち主は1つ(コピーすると、2つが同じものを外そうとする)。
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        // ライブラリを読み込む。読み込んでいたものは先に外す。読めなければ false(理由はログに出す)。
        bool Load(const char* path);

        // 名前で関数を探す。無ければ nullptr(ログに出す)。C++ の関数は名前が崩される(マングリング)ので、
        // 探す関数は extern "C" で書き出しておく。
        void* FindFunction(const char* name) const;

        // 外す。読み込んでいなければ何もしない。
        void Unload();

        bool IsLoaded() const { return m_handle != nullptr; }
        const std::string& GetPath() const { return m_path; }

        // この OS のライブラリのファイル名の終わり(Windows は ".dll"、Mac は ".dylib"、Linux は ".so")。
        static const char* GetFileSuffix();

    private:
        void* m_handle = nullptr; // SDL_SharedObject*(SDL の型は外に出さない)
        std::string m_path;
    };
}
