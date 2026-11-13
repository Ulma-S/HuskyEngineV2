// SceneGrid.h の実装。
#include "tools/SceneGrid.h"

#if HUSKY_DEVELOPMENT

#include <cmath> // std::ceil、std::floor、std::llround

namespace husky::tools
{
    namespace
    {
        // 線の色(背景が暗い紺なので、白を薄く重ねる)。
        constexpr render::Color MinorColor = {1.0f, 1.0f, 1.0f, 0.08f}; // ふつうの線
        constexpr render::Color MajorColor = {1.0f, 1.0f, 1.0f, 0.20f}; // 10 本ごとの線
        constexpr render::Color XAxisColor = {0.9f, 0.3f, 0.3f, 0.60f}; // x 軸(y = 0 の横の線)。Unity と同じく赤
        constexpr render::Color YAxisColor = {0.4f, 0.9f, 0.4f, 0.60f}; // y 軸(x = 0 の縦の線)。Unity と同じく緑

        // 真ん中が (x, y)、大きさ width x height の白い板を1枚足す。
        void AddLine(std::vector<render::Sprite>& out, float x, float y, float width, float height, const render::Color& color)
        {
            render::Sprite line;
            line.model.m[12] = x; // 移動の行列: 4 列目の 1〜2 行目が x、y の移動量(core/Matrix4.h)
            line.model.m[13] = y;
            line.width = width;
            line.height = height;
            line.color = color; // テクスチャは空のまま = 白い板に色を掛ける
            out.push_back(line);
        }
    }

    float ChooseGridSpacing(float zoom, float minPixels)
    {
        if (zoom <= 0.0f)
        {
            zoom = 1.0f;
        }
        // 1 単位は画面で zoom ピクセル。間隔 × zoom が minPixels 以上になるまで、10 倍していく。
        float spacing = 1.0f;
        while (spacing * zoom < minPixels && spacing < 1.0e9f)
        {
            spacing *= 10.0f;
        }
        return spacing;
    }

    void BuildGridSprites(const engine::CameraView& view, float screenWidth, float screenHeight,
                          std::vector<render::Sprite>& out, int maxLinesPerAxis)
    {
        out.clear(); // 中身だけ消す(容量は残るので、次からは確保しない)
        const float zoom = view.zoom > 0.0f ? view.zoom : 1.0f;
        const float spacing = ChooseGridSpacing(zoom);
        const float thickness = 1.0f / zoom; // 画面でいつも 1 ピクセル

        // 見えている範囲(ワールド)
        const float halfWidth = screenWidth * 0.5f / zoom;
        const float halfHeight = screenHeight * 0.5f / zoom;
        const float left = view.centerX - halfWidth;
        const float right = view.centerX + halfWidth;
        const float bottom = view.centerY - halfHeight;
        const float top = view.centerY + halfHeight;

        // 何本目の線か(原点から数えて)を整数で数える。0 なら軸、10 の倍数なら太い区切り。
        // float で「x が 0 か」を比べると誤差で外れることがあるので、整数で判定する。
        auto colorOf = [](long long index, const render::Color& axis) {
            if (index == 0)
            {
                return axis;
            }
            return index % 10 == 0 ? MajorColor : MinorColor;
        };

        // 縦の線(x が一定)。見えている範囲の上下いっぱいに伸ばす
        const long long firstX = static_cast<long long>(std::ceil(left / spacing));
        const long long lastX = static_cast<long long>(std::floor(right / spacing));
        for (long long i = firstX; i <= lastX && i - firstX < maxLinesPerAxis; ++i)
        {
            AddLine(out, static_cast<float>(i) * spacing, view.centerY, thickness, top - bottom, colorOf(i, YAxisColor));
        }
        // 横の線(y が一定)
        const long long firstY = static_cast<long long>(std::ceil(bottom / spacing));
        const long long lastY = static_cast<long long>(std::floor(top / spacing));
        for (long long i = firstY; i <= lastY && i - firstY < maxLinesPerAxis; ++i)
        {
            AddLine(out, view.centerX, static_cast<float>(i) * spacing, right - left, thickness, colorOf(i, XAxisColor));
        }
    }
}

#endif
