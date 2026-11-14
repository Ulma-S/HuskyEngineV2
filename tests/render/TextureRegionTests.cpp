// テクスチャの一部を書き換えるときの、範囲の確かめ(IsTextureRegionInside。UI-2b)のテスト。GPU は使わない。
#include "render/RenderDevice.h"

#include <doctest/doctest.h>

#include <climits> // INT_MAX

using husky::render::IsTextureRegionInside;

TEST_CASE("TextureRegion: テクスチャの中に収まる四角は書き換えられる(端にぴったりも可)")
{
    CHECK(IsTextureRegionInside(64, 32, 0, 0, 64, 32));  // 全体
    CHECK(IsTextureRegionInside(64, 32, 10, 5, 8, 8));   // 内側
    CHECK(IsTextureRegionInside(64, 32, 56, 24, 8, 8));  // 右下の角にぴったり
}

TEST_CASE("TextureRegion: はみ出す・マイナスの位置・大きさ 0 の四角は書き換えられない")
{
    CHECK_FALSE(IsTextureRegionInside(64, 32, 57, 0, 8, 8)); // 右に 1 はみ出す
    CHECK_FALSE(IsTextureRegionInside(64, 32, 0, 25, 8, 8)); // 下に 1 はみ出す
    CHECK_FALSE(IsTextureRegionInside(64, 32, -1, 0, 8, 8));
    CHECK_FALSE(IsTextureRegionInside(64, 32, 0, -1, 8, 8));
    CHECK_FALSE(IsTextureRegionInside(64, 32, 0, 0, 0, 8));
    CHECK_FALSE(IsTextureRegionInside(64, 32, 0, 0, 65, 1)); // テクスチャより大きい
}

TEST_CASE("TextureRegion: 大きな値でも足し算があふれて「収まる」と間違えない")
{
    // x + width を計算すると int があふれてマイナスになり、収まって見えてしまう組み合わせ
    CHECK_FALSE(IsTextureRegionInside(64, 32, INT_MAX, 0, 8, 8));
    CHECK_FALSE(IsTextureRegionInside(64, 32, 0, 0, INT_MAX, 8));
}
