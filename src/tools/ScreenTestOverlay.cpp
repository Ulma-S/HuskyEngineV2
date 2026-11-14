// ScreenTestOverlay.h の実装。
#include "tools/ScreenTestOverlay.h"

#if HUSKY_DEVELOPMENT

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

    void ScreenTestOverlay::Draw(render::RenderDevice& renderDevice) const
    {
        if (!m_visible)
        {
            return;
        }
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
            {(width - size) * 0.5f, (height - size) * 0.5f, {1.0f, 1.0f, 1.0f, 0.8f}}, // 真ん中: 白(四隅は窓に隠れやすいので)
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
    }
}

#endif
