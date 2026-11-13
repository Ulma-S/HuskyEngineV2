// 物理の中身(Box2D)の中のメモリの確保を、エンジンの MemoryTracker に通す(COL-1)。
// ヘッダには Box2D の型を書かない(設計メモ「当たり判定と物理」: Box2D は中身の .cpp の中だけで使う)。
//
// 使い方: 物理の世界を作る前に1回呼ぶ(何度呼んでもよい)。
//   engine::InstallPhysicsMemoryHooks();
//
// なぜ要るか: Box2D は自分で malloc / free を呼ぶので、そのままでは MemoryTracker の数(毎秒のログ)に入らない。
// Box2D には確保の差し替え口(b2SetAllocator)があるので、そこに MemoryTracker の関数を渡す
// (SDL の SDL_SetMemoryFunctions、yyjson の yyjson_alc と同じ考え方)。
//
// 動作スレッド: ゲームスレッド(物理の世界を作る前)。差し替えた後の確保は、Box2D がどのスレッドから呼んでもよい
// (MemoryTracker はスレッドをまたいで数えられる)。
#pragma once

#include <cstddef>

namespace husky::engine
{
    void InstallPhysicsMemoryHooks();

    // 物理の中身が今、確保しているバイト数(Box2D が自分で数えている値)。テストと、ログで使う。
    std::size_t GetPhysicsAllocatedBytes();
}
