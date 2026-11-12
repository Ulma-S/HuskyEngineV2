// SpriteBatch(同じテクスチャが続く区間にまとめる)のテスト。GPU は使わない。
#include "render/SpriteBatch.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using husky::render::AddSpriteToBatches;
using husky::render::SpriteBatch;
using husky::render::TextureHandle;

namespace
{
    // textures の順にスプライトを積んだときの区間を返す。
    std::vector<SpriteBatch> Build(const std::vector<TextureHandle>& textures)
    {
        std::vector<SpriteBatch> batches;
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            AddSpriteToBatches(batches, textures[i], static_cast<std::uint32_t>(i));
        }
        return batches;
    }
}

TEST_CASE("SpriteBatch: 全部同じテクスチャなら、区間は1つ(描画命令1回)")
{
    const TextureHandle a{0, 1};
    const std::vector<SpriteBatch> batches = Build({a, a, a, a});
    REQUIRE(batches.size() == 1);
    CHECK(batches[0].texture == a);
    CHECK(batches[0].firstSprite == 0);
    CHECK(batches[0].spriteCount == 4);
}

TEST_CASE("SpriteBatch: テクスチャが変わるたびに区間を切る。積んだ順(奥から手前)は変えない")
{
    const TextureHandle a{0, 1};
    const TextureHandle b{1, 1};
    // A A A B B A → [A x3] [B x2] [A x1]。最後の A を前の A とまとめると、B より奥に描かれてしまうので、まとめない。
    const std::vector<SpriteBatch> batches = Build({a, a, a, b, b, a});
    REQUIRE(batches.size() == 3);
    CHECK(batches[0].texture == a);
    CHECK(batches[0].firstSprite == 0);
    CHECK(batches[0].spriteCount == 3);
    CHECK(batches[1].texture == b);
    CHECK(batches[1].firstSprite == 3);
    CHECK(batches[1].spriteCount == 2);
    CHECK(batches[2].texture == a);
    CHECK(batches[2].firstSprite == 5);
    CHECK(batches[2].spriteCount == 1);
}

TEST_CASE("SpriteBatch: 同じ場所でも世代が違えば別のテクスチャとして扱う。何も指さないハンドル同士は同じ区間")
{
    const TextureHandle old{0, 1};
    const TextureHandle reused{0, 2}; // 手放された場所に作り直されたテクスチャ
    const TextureHandle none;         // テクスチャ無し(白い板)
    const std::vector<SpriteBatch> batches = Build({old, reused, none, none});
    REQUIRE(batches.size() == 3);
    CHECK(batches[2].texture.IsNull());
    CHECK(batches[2].spriteCount == 2);
}

TEST_CASE("SpriteBatch: 何も積まなければ区間も無い")
{
    CHECK(Build({}).empty());
}
