// 画像ファイルの読み込み。結果は SDL に依存しない Image として返す。
#pragma once

#include <cstdint>
#include <vector>

namespace husky::platform
{
    // CPU のメモリ上にある画像。1ピクセル4バイトで、R, G, B, A の順に並ぶ。
    // 行は上から順に、隙間なく並ぶ(1行 = width * 4 バイト)。
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels; // 大きさは width * height * 4
    };

    // PNG ファイルを読み込み、outImage に入れる。
    // path: exe のあるフォルダからの相対パス(例: "assets/textures/test_sprite.png")。
    //   区切りは「/」で書く(Windows でも動く。パスを Windows 固有の書き方にしないため)。
    // 戻り値: 成功したら true。
    // 動作スレッド: どこでもよい
    bool LoadPng(const char* path, Image& outImage);
}
