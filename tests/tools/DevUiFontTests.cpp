// 開発用の UI の日本語のフォント(FONT-1、assets/fonts/BIZUDGothic-Regular.ttf)のテスト(開発ビルドだけ)。
// GPU を使わずに、ImGui がフォントを読めて、日本語の字を持っていることを確かめる。
#if HUSKY_DEVELOPMENT

#include "HeadlessImGui.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

TEST_CASE("開発用の UI のフォントを ImGui が読め、日本語と英数字の字を持っている")
{
    std::string data;
    const std::string path = husky::platform::GetExecutableDirectory() + "assets/fonts/BIZUDGothic-Regular.ttf";
    REQUIRE(husky::platform::ReadTextFile(path.c_str(), data));

    husky::tests::HeadlessImGui imgui;
    // RenderDevice と同じく、ImGui の確保の関数で借りた場所に写して渡す(ImGui が持ち、自分で解放する)。
    void* copy = ImGui::MemAlloc(data.size());
    std::memcpy(copy, data.data(), data.size());
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, static_cast<int>(data.size()), 15.0f);
    REQUIRE(font != nullptr);

    CHECK(font->IsGlyphInFont(static_cast<ImWchar>(0x65E5))); // 日
    CHECK(font->IsGlyphInFont(static_cast<ImWchar>(0x3042))); // あ
    CHECK(font->IsGlyphInFont(static_cast<ImWchar>(0x30A2))); // ア
    CHECK(font->IsGlyphInFont(static_cast<ImWchar>('A')));
}

#endif
