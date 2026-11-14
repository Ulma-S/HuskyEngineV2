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
#include "engine/Application.h"
#include "engine/GameModule.h"

namespace husky::game
{
    constexpr int ScreenWidth = 1280; // 画面(ウィンドウ)の大きさ(ピクセル)
    constexpr int ScreenHeight = 720;
    // ゲームの世界の大きさ(ワールドの単位。左下が (0, 0))。画面の 3 倍で、カメラがプレイヤーを追って映す。
    // 跳ね回るアクターが跳ね返る端と、プレイヤーが出られない端も、これで決まる。
    constexpr int WorldWidth = ScreenWidth * 3;
    constexpr int WorldHeight = ScreenHeight * 3;
    // 置く数の既定(設定のデータ data/bouncers.sdata の count が読めないときに使う)
    constexpr int BouncerCount = 1000;

    // ゲームの型を、名前から作れるように登録する(engine の型は Application が登録済み)。
    // シーンをファイルから読み込むときに、"type": "BouncerActor" から本物のオブジェクトを作るのに使う。
    // ゲームの exe も、エディタも、シーンを読む前に呼ぶ。
    void RegisterGameTypes(core::TypeRegistry& types);

    // シーンのファイルが無いときの、最初の配置。跳ね回るアクターを設定のデータ(data/bouncers.sdata、BouncerSettings)の
    // count 個(読めなければ BouncerCount 個)、プレイヤーを追うカメラ、プレイヤーを置く。
    // 一覧の順番が描画の順番なので、プレイヤーを最後に置いて一番手前に描く。
    // 跳ね回るアクターとプレイヤーは、assets のプレハブ(prefabs/bouncer.prefab、prefabs/player.prefab)から置き、
    // 1つずつ違う値(位置、速度、色)だけを置いた後に書く。見た目の決め事(大きさ、スプライトシート、アニメーション)はプレハブの中にある。
    // プレハブを読むので、先にアセットの表(app.GetAssetDatabase().Scan)を作っておくこと。プレハブが無ければ、ログに出してそれを置かない。
    // 乱数の種を固定しているので、毎回同じ配置になる。
    void SpawnInitialActors(engine::Application& app);

    // シーンに Canvas(CanvasComponent を持つアクター)が無ければ、HUD のプレハブ(prefabs/hud.prefab)を置く(UI-7)。
    // シーンのファイルを読んだ後も、コードで最初の配置をした後も呼ぶ(F5 で保存すれば、次からはファイルの HUD が使われる)。
    // 先にアセットの表が要る。
    void SpawnHudIfMissing(engine::Application& app);

    // エンジンに渡す関数の表(HR-2。engine/GameModule.h)。開発ビルドの exe は DLL の入口(GameModuleExport.cpp)から、
    // 製品(静的リンク)の exe はこの関数を直接呼んで受け取る。
    void FillGameModule(engine::GameModule& module);
}
