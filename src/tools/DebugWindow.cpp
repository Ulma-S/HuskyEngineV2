// DebugWindow.h の実装。製品(Release)では中身が空になる。
#include "tools/DebugWindow.h"

#if HUSKY_DEVELOPMENT

#include "core/MemoryTracker.h"

#include <imgui.h>

#include <string>

namespace husky::tools
{
    namespace
    {
        // 「Editor commands」の欄: 登録されたコマンドを、登録した順にボタンで並べ、最後に実行した結果を出す。
        void DrawCommands(engine::Application& app, EditorCommandRegistry& commands)
        {
            ImGui::SeparatorText("Editor commands");
            // ボタンを押すと一覧の外のもの(シーン)が変わるので、押されたコマンドは、並べ終わってから実行する。
            const EditorCommand* clicked = nullptr;
            for (const EditorCommand& command : commands.GetCommands())
            {
                // ボタンの文字: "Save scene (F5)"。"##scene.save" の部分は画面に出ない、ボタンを区別するための ID
                // (表示名が同じコマンドがあっても、押し間違えないように)。
                std::string text = command.label;
                if (command.shortcut != engine::Key::Unknown)
                {
                    text += std::string(" (") + GetKeyDisplayName(command.shortcut) + ")";
                }
                text += "##" + command.name;
                // Button は、押されたフレームだけ true を返す。
                if (ImGui::Button(text.c_str()))
                {
                    clicked = &command;
                }
                // マウスを乗せると、何をするかと、CLI や AI から呼ぶときの名前を出す。
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s\n[%s]", command.description.c_str(), command.name.c_str());
                }
            }
            if (clicked != nullptr)
            {
                const std::string name = clicked->name; // 実行中に一覧が変わっても困らないように、名前をコピーしてから呼ぶ
                commands.Execute(name, app);
            }

            // 最後の結果: 成功は緑、失敗は赤。細かい内容は、マウスを乗せると出す。
            if (!commands.GetLastCommandName().empty())
            {
                const EditorCommandResult& result = commands.GetLastResult();
                const ImVec4 color = result.succeeded ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                ImGui::TextColored(color, "%s: %s", result.succeeded ? "OK" : "FAILED", result.message.c_str());
                if (!result.details.empty() && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    for (const std::string& detail : result.details)
                    {
                        ImGui::TextUnformatted(detail.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
        }
    }

    void DrawDebugWindow(engine::Application& app, EditorCommandRegistry* commands)
    {
        // 最初に出す位置(ImGuiCond_FirstUseEver: 初めて出すときだけ。その後はマウスで動かした位置のまま)。
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);

        // 大きさは、毎フレーム中身に合わせる(AlwaysAutoResize)。コマンドの結果の行が増えたり長くなったりしても、
        // 前に保存された窓の大きさ(imgui.ini)のせいで、はみ出して見えなくなることが無いように。
        // Begin が false を返したら、窓が畳まれている。中身は作らない。End は、どちらの場合も必ず呼ぶ(ImGui の決まり)。
        if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

            const render::FrameStats& render = app.GetLastRenderStats();
            ImGui::Text("Sprites: %d  Draw calls: %d  Dropped: %d", render.spriteCount, render.drawCallCount,
                        render.droppedSpriteCount);

            ImGui::SeparatorText("Objects");
            ImGui::Text("Objects: %zu  Actors: %zu", app.GetRegistry().GetObjectCount(), app.GetScene().actors.size());
            ImGui::Text("Collected by last GC: %zu", app.GetCollectedLastGc());

            ImGui::SeparatorText("Memory");
            const core::MemoryStats memory = core::GetMemoryStats();
            const double megabyte = 1024.0 * 1024.0;
            ImGui::Text("Heap: %.2f MB (%zu allocations)", static_cast<double>(memory.currentBytes) / megabyte,
                        memory.currentCount);
            ImGui::Text("Peak: %.2f MB", static_cast<double>(memory.peakBytes) / megabyte);
            const core::FreeListAllocator& objects = app.GetRegistry().GetAllocator();
            ImGui::Text("Object heap: %.2f / %.2f MB", static_cast<double>(objects.GetUsedBytes()) / megabyte,
                        static_cast<double>(objects.GetCapacity()) / megabyte);

            if (commands != nullptr)
            {
                DrawCommands(app, *commands);
            }
        }
        ImGui::End();
    }

    const char* GetKeyDisplayName(engine::Key key)
    {
        switch (key)
        {
        case engine::Key::W: return "W";
        case engine::Key::A: return "A";
        case engine::Key::S: return "S";
        case engine::Key::D: return "D";
        case engine::Key::Up: return "Up";
        case engine::Key::Down: return "Down";
        case engine::Key::Left: return "Left";
        case engine::Key::Right: return "Right";
        case engine::Key::Space: return "Space";
        case engine::Key::Enter: return "Enter";
        case engine::Key::Escape: return "Escape";
        case engine::Key::F5: return "F5";
        case engine::Key::Unknown:
        case engine::Key::Count: break;
        }
        return "";
    }
}

#endif
