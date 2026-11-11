// OS のウィンドウ1枚を表すクラス。
// このヘッダには SDL の型を書かない。SDL のウィンドウは Window.cpp の中の Impl だけが持つ。
#pragma once

#include <memory> // std::unique_ptr

namespace husky::platform
{
    // ウィンドウを開くときの設定。
    struct WindowDesc
    {
        const char* title = "HuskyEngineV2";
        int width = 1280;
        int height = 720;
    };

    // OS のウィンドウ1枚。
    // RAII: このオブジェクトが破棄されると(スコープを抜けると)、ウィンドウも自動で閉じる。
    // C# の IDisposable + using に近いが、C++ ではデストラクタが「必ず」「その場で」呼ばれる。
    // 動作スレッド: メインスレッド
    class Window final
    {
    public:
        Window();
        // デストラクタ。ウィンドウが開いていれば閉じる。
        // 中身は Window.cpp に書く(Impl の中身が分かるのは .cpp だけなので、そこで破棄する必要がある)。
        ~Window();

        // コピーを禁止する。コピーできると、1枚のウィンドウを2つのオブジェクトが閉じようとしてしまう。
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // ウィンドウを開く。戻り値: 成功したら true。
        bool Open(const WindowDesc& desc);

        // 描画層の実装だけが使う。中身は SDL_Window* だが、ヘッダに SDL の型を出さないため void* で返す。
        // void* は「何かを指すポインタ(型は不明)」。使う側(描画層の .cpp)で元の型に戻して使う。
        // ゲームや engine からは使わないこと。開いていなければ nullptr。
        void* GetNativeHandle() const;

    private:
        // Pimpl: 中身(SDL のウィンドウなど)は Impl に入れ、その定義は Window.cpp だけに書く。
        // ここでは「Impl という型がある」とだけ宣言する(前方宣言)。
        struct Impl;

        // Impl の持ち主はこの Window だけ。Window が破棄されると Impl も自動で解放される。
        std::unique_ptr<Impl> m_impl;
    };
}
