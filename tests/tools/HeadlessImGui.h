// テストで使う、画面も GPU も持たない ImGui(開発ビルドだけ)。tools のテスト(インスペクタ、デバッグの窓)で共通に使う。
// ImGui は、使い方の誤り(PushID と PopID の数の食い違い、Begin と End の対応など)を、自分の確認(IM_ASSERT)で止めるので、
// 「最後まで動く」こと自体が、使い方が正しいことの確認になる。マウスでの操作は、ゲームを起動して確かめる。
#pragma once

#if HUSKY_DEVELOPMENT

#include <imgui.h>

namespace husky::tests
{
    // 画面を持たない ImGui を1つ用意し、テストが終わったら片付ける(コンストラクタで用意し、デストラクタで片付ける RAII)。
    class HeadlessImGui
    {
    public:
        HeadlessImGui()
        {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            // 「テクスチャの作成は描画の部品が受け持つ」と伝える(実際には何もしない)。こうすると、
            // フォントの画像を GPU に送る部品が無くても、ImGui はフレームを進められる。
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        }
        ~HeadlessImGui() { ImGui::DestroyContext(); }

        HeadlessImGui(const HeadlessImGui&) = delete;
        HeadlessImGui& operator=(const HeadlessImGui&) = delete;

        // 1フレーム分: NewFrame → draw() で UI を作る → Render。
        template <typename Function>
        void Frame(Function&& draw)
        {
            ImGui::NewFrame();
            draw();
            ImGui::Render();
        }
    };
}

#endif
