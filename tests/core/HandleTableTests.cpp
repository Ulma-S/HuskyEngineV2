// Handle と HandleTable のテスト。
#include "core/HandleTable.h"

#include <doctest/doctest.h>

#include <string>
#include <type_traits> // std::is_convertible_v

using husky::core::Handle;
using husky::core::HandleTable;

namespace
{
    // テスト用の型。ハンドルの「種類」を区別するためだけに使う。
    struct Enemy
    {
        std::string name;
        int hp = 0;
    };
    struct Texture
    {
    };
}

// static_assert はコンパイル時の確認。成り立たなければ、テストを実行する前にビルドが失敗する。
// Handle<Enemy> を Handle<Texture> として使えない(取り違えるとコンパイルエラーになる)ことを確かめる。
static_assert(!std::is_convertible_v<Handle<Enemy>, Handle<Texture>>,
              "種類の違うハンドルは、取り違えられないこと");

TEST_CASE("Handle: 何も設定していないハンドルは、どこも指していない")
{
    Handle<Enemy> handle;
    CHECK(handle.IsNull());

    HandleTable<Enemy> table;
    CHECK(table.Get(handle) == nullptr);
    CHECK_FALSE(table.Contains(handle));
}

TEST_CASE("HandleTable: Add で返ったハンドルから、中身を引ける")
{
    HandleTable<Enemy> table;
    const Handle<Enemy> slime = table.Add(Enemy{"スライム", 10});
    const Handle<Enemy> bat = table.Add(Enemy{"コウモリ", 5});

    CHECK(table.Size() == 2);
    CHECK_FALSE(slime.IsNull());
    CHECK(slime != bat);

    // REQUIRE は CHECK と違い、失敗したらこのテストをその場で打ち切る。
    // 次の行で nullptr を触ってしまわないように、先に確かめておく。
    REQUIRE(table.Get(slime) != nullptr);
    CHECK(table.Get(slime)->name == "スライム");
    CHECK(table.Get(bat)->hp == 5);

    // Get で返ったポインタを通して、中身を書き換えられる。
    table.Get(slime)->hp = 3;
    CHECK(table.Get(slime)->hp == 3);
}

TEST_CASE("HandleTable: Remove した後の古いハンドルは、nullptr を返す")
{
    HandleTable<Enemy> table;
    const Handle<Enemy> slime = table.Add(Enemy{"スライム", 10});

    CHECK(table.Remove(slime));
    CHECK(table.Get(slime) == nullptr);
    CHECK_FALSE(table.Contains(slime));
    CHECK(table.Size() == 0);

    // 2回目の Remove は、もう無いので false。
    CHECK_FALSE(table.Remove(slime));
}

TEST_CASE("HandleTable: 場所が再利用されても、古いハンドルは新しい中身を指さない")
{
    HandleTable<Enemy> table;
    const Handle<Enemy> slime = table.Add(Enemy{"スライム", 10});
    table.Remove(slime);

    // 空いた場所が再利用される。
    const Handle<Enemy> dragon = table.Add(Enemy{"ドラゴン", 999});
    CHECK(dragon.index == slime.index);           // 場所は同じ
    CHECK(dragon.generation != slime.generation); // 世代は違う

    // ポインタなら、ここで黙ってドラゴンを指してしまうところ。ハンドルは見分けられる。
    CHECK(table.Get(slime) == nullptr);
    REQUIRE(table.Get(dragon) != nullptr);
    CHECK(table.Get(dragon)->name == "ドラゴン");
}

TEST_CASE("HandleTable: 別の表から来たハンドルや、範囲外のハンドルは無効")
{
    HandleTable<Enemy> table;
    table.Add(Enemy{"スライム", 10});

    Handle<Enemy> outOfRange;
    outOfRange.index = 100;
    outOfRange.generation = 1;
    CHECK(table.Get(outOfRange) == nullptr);
    CHECK_FALSE(table.Remove(outOfRange));
}

TEST_CASE("HandleTable: たくさん足して消しても、数と中身が合う")
{
    HandleTable<int> table;
    std::vector<Handle<int>> handles;
    for (int i = 0; i < 100; ++i)
    {
        handles.push_back(table.Add(i));
    }
    // 偶数番目だけ消す。
    for (int i = 0; i < 100; i += 2)
    {
        table.Remove(handles[static_cast<std::size_t>(i)]);
    }
    CHECK(table.Size() == 50);

    for (int i = 0; i < 100; ++i)
    {
        const int* value = table.Get(handles[static_cast<std::size_t>(i)]);
        if (i % 2 == 0)
        {
            CHECK(value == nullptr);
        }
        else
        {
            REQUIRE(value != nullptr);
            CHECK(*value == i);
        }
    }

    // 消した 50 個分の場所が再利用され、表は大きくならない(新しい中身はすべて引ける)。
    for (int i = 0; i < 50; ++i)
    {
        const Handle<int> handle = table.Add(1000 + i);
        CHECK(handle.index < 100);
        CHECK(table.Get(handle) != nullptr);
    }
    CHECK(table.Size() == 100);
}
