// Window.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/Window.h"
#include "platform/PlatformLog.h"

#include <SDL3/SDL.h>

namespace husky::platform
{
    // Window の本当の中身。この定義は、この .cpp の中にしかない。
    struct Window::Impl
    {
        // SDL が作ったウィンドウ。SDL_CreateWindow で作り、SDL_DestroyWindow で壊す。
        // SDL は C のライブラリなので生ポインタで返してくる。所有権は Impl が持ち、デストラクタで必ず返す。
        SDL_Window* sdlWindow = nullptr;

        ~Impl()
        {
            if (sdlWindow != nullptr)
            {
                SDL_DestroyWindow(sdlWindow);
            }
        }
    };

    // std::make_unique で Impl を作る。生の new は書かない。
    Window::Window()
        : m_impl(std::make_unique<Impl>())
    {
    }

    // = default は「コンパイラが作る標準の処理でよい」という意味。
    // 標準の処理は m_impl を破棄し、そこから Impl のデストラクタ(SDL_DestroyWindow)が呼ばれる。
    // ヘッダではなくここに書くのは、Impl の中身を知っているこの場所でないと、Impl を破棄できないから。
    Window::~Window() = default;

    void* Window::GetNativeHandle() const
    {
        return m_impl->sdlWindow;
    }

    bool Window::Open(const WindowDesc& desc)
    {
        // 2回開くと、前のウィンドウを壊す機会がなくなる(リーク)ので、開いていたら断る。
        if (m_impl->sdlWindow != nullptr)
        {
            HUSKY_LOG(PlatformLog::WindowAlreadyOpen);
            return false;
        }

        // 最後のフラグ 0 は「特別な指定なし」。サイズ変更や全画面は、必要になったら足す。
        m_impl->sdlWindow = SDL_CreateWindow(desc.title, desc.width, desc.height, 0);
        if (m_impl->sdlWindow == nullptr)
        {
            HUSKY_LOG(PlatformLog::SdlCallFailed, "SDL_CreateWindow", SDL_GetError());
            return false;
        }
        return true;
    }
}
