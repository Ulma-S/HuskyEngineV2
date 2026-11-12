// DebugWindow.h の実装。製品(Release)では中身が空になる。
#include "tools/DebugWindow.h"

#if HUSKY_DEVELOPMENT

#include "core/MemoryTracker.h"

#include <imgui.h>

namespace husky::tools
{
    void DrawDebugWindow(engine::Application& app, const DebugWindowActions& actions)
    {
        // 最初に出す位置と大きさ(ImGuiCond_FirstUseEver: 初めて出すときだけ。その後はマウスで動かした位置のまま)。
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);

        // Begin が false を返したら、窓が畳まれている。中身は作らない。End は、どちらの場合も必ず呼ぶ(ImGui の決まり)。
        if (ImGui::Begin("Debug"))
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

            if (actions.saveScene)
            {
                ImGui::SeparatorText("Scene");
                // Button は、押されたフレームだけ true を返す。
                if (ImGui::Button("Save scene (F5)"))
                {
                    actions.saveScene();
                }
            }
        }
        ImGui::End();
    }
}

#endif
