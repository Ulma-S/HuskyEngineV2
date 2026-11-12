// Image.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/Image.h"

#include <SDL3/SDL.h>

#include <cstring> // std::memcpy

namespace husky::platform
{
    bool LoadPng(const char* path, Image& outImage)
    {
        // PNG を読み込む。SDL_Surface は SDL の「CPU 上の画像」。
        // (以前は、ここで exe のフォルダを前に付けていた。アセットのパスは AssetDatabase が exe の隣の assets から
        //  作るようになったので、ほかのファイルの関数と同じく、渡されたパスをそのまま使う)
        SDL_Surface* loaded = SDL_LoadPNG(path);
        if (loaded == nullptr)
        {
            SDL_Log("SDL_LoadPNG に失敗しました (%s): %s", path, SDL_GetError());
            return false;
        }

        // ピクセルの並びは PNG によって違う(RGB だけ、パレット付き、など)。
        // 「R, G, B, A の順に1バイトずつ」に揃える。
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (rgba == nullptr)
        {
            SDL_Log("SDL_ConvertSurface に失敗しました: %s", SDL_GetError());
            return false;
        }

        outImage.width = rgba->w;
        outImage.height = rgba->h;
        const std::size_t rowBytes = static_cast<std::size_t>(rgba->w) * 4;
        outImage.pixels.resize(rowBytes * static_cast<std::size_t>(rgba->h));

        // SDL_Surface の1行は、末尾に詰め物があって rowBytes より長いことがある(pitch が1行の本当の長さ)。
        // そのため1行ずつコピーして、隙間なく詰める。
        const auto* source = static_cast<const std::uint8_t*>(rgba->pixels);
        for (int y = 0; y < rgba->h; ++y)
        {
            std::memcpy(outImage.pixels.data() + rowBytes * static_cast<std::size_t>(y),
                        source + static_cast<std::size_t>(rgba->pitch) * static_cast<std::size_t>(y),
                        rowBytes);
        }

        SDL_DestroySurface(rgba);
        return true;
    }
}
