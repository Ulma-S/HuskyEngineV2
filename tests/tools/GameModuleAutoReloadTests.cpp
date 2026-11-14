// HR-5: ゲームの DLL の見張り。「読み直すか」の決め方(DllChangeDetector)は時刻を渡すので、決まった結果になる。
#include "platform/FileTimestamp.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <thread>

#if HUSKY_DEVELOPMENT
#include "tools/GameModuleCommands.h"

using husky::tools::DllChangeDetector;
using Action = husky::tools::DllChangeDetector::Action;

TEST_CASE("HR-5: 見るのは 0.5 秒ごと")
{
    DllChangeDetector detector;
    detector.Reset(100);
    CHECK(detector.IsCheckDue(0.0));
    CHECK_FALSE(detector.IsCheckDue(0.2));
    CHECK(detector.IsCheckDue(0.5));
    CHECK_FALSE(detector.IsCheckDue(0.9));
    CHECK(detector.IsCheckDue(1.1));
}

TEST_CASE("HR-5: 日時が変わり、同じ日時が 0.5 秒続いたら読み直す。同じ日時では繰り返さない")
{
    DllChangeDetector detector;
    detector.Reset(100);
    CHECK(detector.Update(0.0, true, 100, true) == Action::None); // 変わっていない
    CHECK(detector.Update(0.5, true, 200, true) == Action::None); // 変わった(落ち着くのを待つ)
    CHECK(detector.Update(0.7, true, 200, true) == Action::None); // まだ 0.2 秒
    CHECK(detector.Update(1.0, true, 200, true) == Action::Reload);
    CHECK(detector.Update(1.5, true, 200, true) == Action::None); // 読み直した日時になったので、もう読まない
}

TEST_CASE("HR-5: 書いている途中で日時がまた変わったり、読めなかったりしたら、待ち直す")
{
    DllChangeDetector detector;
    detector.Reset(100);
    CHECK(detector.Update(0.0, true, 200, true) == Action::None);
    CHECK(detector.Update(0.5, true, 300, true) == Action::None); // また変わった: ここから待ち直す
    CHECK(detector.Update(0.9, true, 300, true) == Action::None);
    CHECK(detector.Update(1.0, false, 0, true) == Action::None);  // 読めない(リンカが書いている)
    CHECK(detector.Update(1.5, true, 300, true) == Action::None); // 読めたので、ここから待ち直す
    CHECK(detector.Update(2.0, true, 300, true) == Action::Reload);
}

TEST_CASE("HR-5: 再生中は読み直さず1回だけ知らせ、編集に戻ったら読み直す")
{
    DllChangeDetector detector;
    detector.Reset(100);
    CHECK(detector.Update(0.0, true, 200, false) == Action::None);
    CHECK(detector.Update(0.5, true, 200, false) == Action::WaitForEdit);
    CHECK(detector.Update(1.0, true, 200, false) == Action::None); // 2回目は知らせない
    CHECK(detector.Update(1.5, true, 200, true) == Action::Reload); // Stop した
}
#endif

TEST_CASE("HR-5: FileTimestamp はファイルの更新日時を読み、書き直すと変わる。無いファイルは false")
{
    husky::tests::TestFolder folder("husky_test_file_timestamp");
    folder.Write("a.txt", "one");
    husky::platform::FileTimestamp file;
    file.SetPath(folder.PathOf("a.txt"));
    std::int64_t first = 0;
    REQUIRE(file.Read(first));

    // 書き直す(日時の細かさは OS による。変わったことを確かめるため、最大 2 秒まで書き直しを繰り返す)
    std::int64_t second = first;
    for (int i = 0; i < 200 && second == first; ++i)
    {
        folder.Write("a.txt", "two");
        file.Read(second);
        if (second == first)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    CHECK(second != first);

    husky::platform::FileTimestamp missing;
    missing.SetPath(folder.PathOf("missing.txt"));
    std::int64_t time = 0;
    CHECK_FALSE(missing.Read(time));
}
