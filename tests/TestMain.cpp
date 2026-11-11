// テスト用 exe のエントリポイント。
// このマクロを定義してから doctest.h を読むと、doctest が main 関数を自動で作ってくれる。
// その main は、ほかの .cpp に書いた TEST_CASE をすべて見つけて実行し、1つでも失敗したら 0 以外を返す。
// (マクロ = コンパイルの前に、文字の置き換えでコードを作る仕組み。C# にはない C/C++ 特有のもの)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
