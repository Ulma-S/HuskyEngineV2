// ログのカタログ(log.catalog、tools/LogCatalog.cpp)のテスト(開発ビルドだけ)。
#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

#include "core/Log.h"

#include <doctest/doctest.h>

#include <string>

namespace catalogtest
{
    enum class CatalogTestLog : std::uint16_t
    {
        First = 1,
        Third = 3,
    };

    HUSKY_LOG_CHANNEL_BEGIN(CatalogTestLog, "CatalogTest.Inner")
        HUSKY_LOG_MESSAGE(First, Info, "最初 {value}", HUSKY_LOG_ARG(value, Int))
        HUSKY_LOG_MESSAGE(Third, Error, "三番目")
    HUSKY_LOG_CHANNEL_END()
}

namespace
{
    const husky::engine::ArchiveValue* FindChannel(const husky::engine::ArchiveValue& catalog, const char* name)
    {
        const husky::engine::ArchiveValue* channels = catalog.Find("channels");
        for (std::size_t i = 0; channels != nullptr && i < channels->GetArraySize(); ++i)
        {
            if (channels->At(i).Find("name")->AsString() == name)
                return &channels->At(i);
        }
        return nullptr;
    }
}

TEST_CASE("カタログには、登録した全部のチャンネル(親だけのものも)とメッセージの定義が載る")
{
    const husky::engine::ArchiveValue catalog = husky::tools::BuildLogCatalog();
    CHECK(catalog.Find("format")->AsInt() == 1);

    // 親だけのチャンネルは、メッセージが空で、parent が null。
    const husky::engine::ArchiveValue* parent = FindChannel(catalog, "CatalogTest");
    REQUIRE(parent != nullptr);
    CHECK(parent->Find("parent")->IsNull());
    CHECK(parent->Find("messages")->GetArraySize() == 0);

    const husky::engine::ArchiveValue* inner = FindChannel(catalog, "CatalogTest.Inner");
    REQUIRE(inner != nullptr);
    CHECK(inner->Find("parent")->AsString() == "CatalogTest");
    const husky::engine::ArchiveValue& messages = *inner->Find("messages");
    REQUIRE(messages.GetArraySize() == 2);
    const husky::engine::ArchiveValue& first = messages.At(0);
    CHECK(first.Find("id")->AsInt() == 1);
    CHECK(first.Find("name")->AsString() == "First");
    CHECK(first.Find("level")->AsString() == "Info");
    CHECK(first.Find("format")->AsString() == "最初 {value}");
    REQUIRE(first.Find("args")->GetArraySize() == 1);
    CHECK(first.Find("args")->At(0).Find("name")->AsString() == "value");
    CHECK(first.Find("args")->At(0).Find("type")->AsString() == "Int");
    CHECK(messages.At(1).Find("id")->AsInt() == 3); // 番号の小さい順(すき間はそのまま)

    // エンジンのチャンネルも載っている。
    CHECK(FindChannel(catalog, "Engine.Log") != nullptr);
}

#endif
