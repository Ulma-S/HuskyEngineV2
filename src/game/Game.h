// ゲームのコード(src/game): このゲームの型と、最初の配置。エンジンを使う側(アプリケーション)の本体。
//
// ゲームは、エンジンの部品(core / platform / render / engine / tools)のように付け外しするものではない。
// ただし「ゲームのコード」と「それを起動する exe」は分けておく:
//   - ゲームのコード(このフォルダ、静的ライブラリ husky_game)… 型、ロジック、型の登録
//   - 起動する exe(src/main.cpp)… ウィンドウを開いて、ゲームを動かすだけ
//   - エディタ(HuskyEditorCli.exe など)… ゲームのコードを読み込み、ゲームの型を名前から作れるようにする
// シーンのファイルには "type": "BouncerActor" のようにゲームの型の名前が入るので、シーンを扱うエディタもゲームの型を知る必要がある。
// Unity(エディタもプレイヤーも Assembly-CSharp.dll を読む)、UE(エディタもゲームもプロジェクトのゲームのコードを読む)と同じ形。
// 将来のホットリロードでは、このゲームのコードが DLL になる。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/TypeRegistry.h"
#include "engine/AssetDatabase.h"
#include "engine/Scene.h"

namespace husky::game
{
    constexpr int ScreenWidth = 1280; // 画面(ウィンドウ)の大きさ。跳ね返る端もこれで決まる
    constexpr int ScreenHeight = 720;
    constexpr int BouncerCount = 1000;

    // ゲームの型を、名前から作れるように登録する(engine の型は Application が登録済み)。
    // シーンをファイルから読み込むときに、"type": "BouncerActor" から本物のオブジェクトを作るのに使う。
    // ゲームの exe も、エディタも、シーンを読む前に呼ぶ。
    void RegisterGameTypes(core::TypeRegistry& types);

    // シーンのファイルが無いときの、最初の配置。跳ね回るアクターを BouncerCount 個と、プレイヤーを1つ置く。
    // 一覧の順番が描画の順番なので、プレイヤーを最後に置いて一番手前に描く。
    // 跳ね回るアクターはテクスチャを指さない(既定のテクスチャになる)。プレイヤーだけ、スプライトシート(shapes.spritesheet)の
    // 切り出しを貼る。最初は右下のひし形(diamond)で、アニメーション「cycle」が 4 つの図形を順に切り替える。
    // 乱数の種を固定しているので、毎回同じ配置になる。
    void SpawnInitialActors(engine::Scene& scene, const engine::AssetDatabase& assets);
}
