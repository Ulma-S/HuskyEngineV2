// engine が持っている、ファイルから作れる型を、型の登録簿(core::TypeRegistry)に登録する。
// Application は、作ったときにこれを呼ぶ。ゲームの型は、ゲームの側で登録する(main.cpp など)。
//
// engine に新しい型(コンポーネントなど)を足したら、ここにも1行足すこと。
// 足し忘れると、その型を含むシーンを読み込むときに「知らない型」になる(テストで気づける)。
//
// 動作スレッド: 起動時にゲームスレッドで。
#pragma once

#include "core/TypeRegistry.h"

namespace husky::engine
{
    void RegisterEngineTypes(core::TypeRegistry& types);
}
