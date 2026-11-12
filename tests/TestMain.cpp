// テスト用 exe のエントリポイント。
// DOCTEST_CONFIG_IMPLEMENT を定義してから doctest.h を読むと、doctest の本体がここに作られる。
// main は自分で書き、doctest::Context に、ほかの .cpp に書いた TEST_CASE をすべて実行させる(1つでも失敗したら 0 以外を返す)。
// (以前は DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN で main ごと作らせていたが、テストより先に
//  InstallMemoryHooks を呼ぶ必要ができたので、main を自分で書くようにした)
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "platform/MemoryHooks.h"

int main(int argc, char** argv)
{
    // 一番最初に: SDL のメモリの確保を MemoryTracker に通す(ゲームの main と同じ。platform/MemoryHooks.h)。
    husky::platform::InstallMemoryHooks();

    doctest::Context context(argc, argv); // コマンドラインの指定(-tc="名前" など)を渡す
    return context.run();
}
