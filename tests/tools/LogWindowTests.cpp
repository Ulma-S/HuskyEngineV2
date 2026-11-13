// ログの窓(tools/LogWindow.h、LOG-4)のテスト(開発ビルドだけ)。絞り込みは関数で確かめ、描画は GPU 無しの ImGui で通す。
#if HUSKY_DEVELOPMENT

#include "tools/LogWindow.h"

#include "HeadlessImGui.h"

#include <doctest/doctest.h>

#include <string>

using husky::core::LogLevel;
using husky::core::ScopedLogSink;
using husky::tools::LogWindow;

namespace logwindowtest
{
    enum class WindowTestLog : std::uint16_t
    {
        Hello = 1,
        Detail = 2,
    };

    HUSKY_LOG_CHANNEL_BEGIN(WindowTestLog, "WindowTest.Enemy.System")
        HUSKY_LOG_MESSAGE(Hello, Warning, "こんにちは {name}", HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(Detail, Verbose, "細かい {value}", HUSKY_LOG_ARG(value, Int))
    HUSKY_LOG_CHANNEL_END()

    enum class OtherTestLog : std::uint16_t
    {
        Other = 1,
    };

    HUSKY_LOG_CHANNEL_BEGIN(OtherTestLog, "WindowTest.Other")
        HUSKY_LOG_MESSAGE(Other, Info, "ほか")
    HUSKY_LOG_CHANNEL_END()
}

TEST_CASE("LogWindow はレベル・入れ子のチャンネル・番号・文字で絞り込む")
{
    LogWindow window;
    {
        ScopedLogSink attach(window);
        HUSKY_LOG(logwindowtest::WindowTestLog::Hello, "player");
        HUSKY_LOG(logwindowtest::WindowTestLog::Detail, 7);
        HUSKY_LOG(logwindowtest::OtherTestLog::Other);
    }
    window.SetChannelFilter("WindowTest");

    // 既定は Verbose を隠す。
    window.RefreshVisible();
    REQUIRE(window.GetVisibleCount() == 2);
    CHECK(window.GetVisibleText(0) == "[Warning][WindowTest.Enemy.System] こんにちは player");
    CHECK(window.GetVisibleText(1) == "[Info][WindowTest.Other] ほか");
    CHECK(window.GetCount(LogLevel::Verbose) == 1); // 隠していても数える

    window.SetLevelShown(LogLevel::Verbose, true);
    window.RefreshVisible();
    CHECK(window.GetVisibleCount() == 3);

    // チャンネルを選ぶと、その下だけ。
    window.SetChannelFilter("WindowTest.Enemy");
    window.RefreshVisible();
    CHECK(window.GetVisibleCount() == 2);

    // 番号で絞る。
    window.SetIdFilter(2);
    window.RefreshVisible();
    REQUIRE(window.GetVisibleCount() == 1);
    CHECK(window.GetVisibleText(0) == "[Verbose][WindowTest.Enemy.System] 細かい 7");
    window.SetIdFilter(0);

    // 文字で探す。
    window.SetChannelFilter("");
    window.SetSearch("こんにちは");
    window.RefreshVisible();
    CHECK(window.GetVisibleCount() == 1);

    // 登録されていないチャンネルの名前なら何も出ない。
    window.SetSearch("");
    window.SetChannelFilter("NoSuchChannel");
    window.RefreshVisible();
    CHECK(window.GetVisibleCount() == 0);

    window.SetChannelFilter("WindowTest");
    window.Clear();
    window.RefreshVisible();
    CHECK(window.GetVisibleCount() == 0);
    CHECK(window.GetCount(LogLevel::Warning) == 0);
}

TEST_CASE("LogWindow は入れ物がいっぱいなら、いちばん古い行から消える")
{
    LogWindow window(3);
    {
        ScopedLogSink attach(window);
        for (int i = 0; i < 5; ++i)
            HUSKY_LOG(logwindowtest::WindowTestLog::Detail, i);
    }
    window.SetLevelShown(LogLevel::Verbose, true);
    window.SetChannelFilter("WindowTest");
    window.RefreshVisible();
    REQUIRE(window.GetVisibleCount() == 3);
    CHECK(window.GetVisibleText(0) == "[Verbose][WindowTest.Enemy.System] 細かい 2");
    CHECK(window.GetVisibleText(2) == "[Verbose][WindowTest.Enemy.System] 細かい 4");
    CHECK(window.GetCount(LogLevel::Verbose) == 5);
}

TEST_CASE("LogWindow の Draw は GPU 無しの ImGui でも描ける")
{
    LogWindow window;
    {
        ScopedLogSink attach(window);
        HUSKY_LOG(logwindowtest::WindowTestLog::Hello, "a");
    }
    husky::tests::HeadlessImGui imgui;
    imgui.Frame([&] { window.Draw(); });
    imgui.Frame([&] { window.Draw(); });
    CHECK(window.GetCount(LogLevel::Warning) == 1);
}

#endif
