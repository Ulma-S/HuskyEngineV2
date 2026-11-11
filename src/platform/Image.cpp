// Image.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/Image.h"

#include <SDL3/SDL.h>

#include <cstring> // std::memcpy
#include <string>

namespace husky::platform
{
    bool LoadPng(const char* path, Image& outImage)
    {
        // exe のあるフォルダ(末尾に区切り文字が付いている)。アセットはビルド時にここへコピーしてある。
        // 作業フォルダ(起動のしかたで変わる)ではなく、exe の場所を基準にするのがポイント。
        const char* basePath = SDL_GetBasePath();
        const std::string fullPath = std::string(basePath != nullptr ? basePath : "") + path;

        // PNG を読み込む。SDL_Surface は SDL の「CPU 上の画像」。
        SDL_Surface* loaded = SDL_LoadPNG(fullPath.c_str());
        if (loaded == nullptr)
        {
            SDL_Log("SDL_LoadPNG に失敗しました (%s): %s", fullPath.c_str(), SDL_GetError());
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
