// ScreenTestOverlay.h の実装。
#include "tools/ScreenTestOverlay.h"

#if HUSKY_DEVELOPMENT

#include <cstddef> // std::size_t
#include <cstdint>
#include <utility> // std::move

namespace husky::tools
{
    void ScreenTestOverlay::RegisterCommands(EditorCommandRegistry& commands)
    {
        EditorCommand command;
        command.name = "view.screenTest";
        command.label = "Toggle screen test";
        command.description = "Show or hide squares at the four corners and the center of the screen, drawn in screen coordinates. "
                              "They should stay still when the camera moves.";
        command.execute = [this](EditorCommandContext&) {
            m_visible = !m_visible;
            return EditorCommandResult::Success(m_visible ? "Screen test: shown" : "Screen test: hidden");
        };
        commands.Register(std::move(command));
    }

    void ScreenTestOverlay::Draw(render::RenderDevice& renderDevice)
    {
        if (!m_visible)
        {
            return;
        }
        constexpr int textureSize = 64;
        constexpr int cellSize = 8;
        constexpr int cellsPerRow = textureSize / cellSize;
        if (!renderDevice.IsTextureAlive(m_texture))
        {
            m_texture = renderDevice.CreateTexture(textureSize, textureSize); // 透明で始まる
            m_cell.width = cellSize;
            m_cell.height = cellSize;
            m_cell.pixels.assign(static_cast<std::size_t>(cellSize * cellSize * 4), 0);
        }
        // 30 フレームごとに、ます目を1つ不透明な色で塗る(色は番号で変える)。64 個塗ったら、また最初から塗り直す。
        if (m_frame % 30 == 0)
        {
            const std::uint8_t r = static_cast<std::uint8_t>(64 + (m_nextCell * 37) % 192);
            const std::uint8_t g = static_cast<std::uint8_t>(64 + (m_nextCell * 71) % 192);
            const std::uint8_t b = static_cast<std::uint8_t>(64 + (m_nextCell * 113) % 192);
            for (std::size_t i = 0; i < m_cell.pixels.size(); i += 4)
            {
                m_cell.pixels[i + 0] = r;
                m_cell.pixels[i + 1] = g;
                m_cell.pixels[i + 2] = b;
                m_cell.pixels[i + 3] = 255;
            }
            const int x = (m_nextCell % cellsPerRow) * cellSize;
            const int y = (m_nextCell / cellsPerRow) * cellSize;
            renderDevice.UpdateTexture(m_texture, x, y, m_cell);
            m_nextCell = (m_nextCell + 1) % (cellsPerRow * cellsPerRow);
        }
        ++m_frame;

        constexpr float size = 48.0f;
        constexpr float margin = 16.0f;
        const float width = static_cast<float>(renderDevice.GetScreenWidth());
        const float height = static_cast<float>(renderDevice.GetScreenHeight());

        // 四隅の左下の角の位置(画面ちょうどのカメラなので、左下が (0, 0)、上が +y)と色。
        struct Corner
        {
            float x;
            float y;
            render::Color color;
        };
        const Corner corners[] = {
            {margin, margin, {1.0f, 0.3f, 0.3f, 0.8f}},                                 // 左下: 赤
            {width - margin - size, margin, {0.3f, 1.0f, 0.3f, 0.8f}},                  // 右下: 緑
            {margin, height - margin - size, {0.3f, 0.5f, 1.0f, 0.8f}},                 // 左上: 青
            {width - margin - size, height - margin - size, {1.0f, 1.0f, 0.3f, 0.8f}}, // 右上: 黄
        };
        for (const Corner& corner : corners)
        {
            render::Sprite sprite;
            // 移動だけの行列: 列優先なので、移動の値は 12・13 番(4 列目)に入る
            sprite.model.m[12] = corner.x;
            sprite.model.m[13] = corner.y;
            sprite.width = size;
            sprite.height = size;
            sprite.pivotX = 0.0f; // 左下の角を位置に合わせる
            sprite.pivotY = 0.0f;
            sprite.color = corner.color;
            renderDevice.DrawSprite(sprite); // テクスチャ無し = 白い板に色を掛ける
        }

        // 真ん中: 試しのテクスチャを 2 倍(128 ピクセル)で貼る。まだ塗っていないます目は透明で、後ろのシーンが見える。
        // 四隅は窓に隠れやすいので、真ん中で確かめる。
        render::Sprite center;
        center.model.m[12] = width * 0.5f;
        center.model.m[13] = height * 0.5f;
        center.width = textureSize * 2.0f;
        center.height = textureSize * 2.0f;
        center.texture = m_texture;
        renderDevice.DrawSprite(center);
    }
}

#endif
