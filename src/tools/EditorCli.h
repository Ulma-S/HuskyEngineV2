// エディタの CLI(コマンドラインツール): 画面を開かずに、エディタのコマンドを名前で1回だけ実行する。
// Unity の -batchmode -executeMethod、UE のコマンドレットにあたる。開発ビルドだけ(#if HUSKY_DEVELOPMENT)。
//
// 使い方(exe は HuskyEditorCli.exe。中身はこの RunEditorCli を呼ぶだけ):
//   HuskyEditorCli --help                          使い方を出す
//   HuskyEditorCli --list                          使えるコマンドの名前と説明を出す
//   HuskyEditorCli asset.refresh                   コマンドを実行する
//   HuskyEditorCli some.command "{\"x\": 1}"       引数(JSON のオブジェクト)を渡して実行する
// オプション(どの使い方にも付けられる):
//   --assets <フォルダ>     アセットのフォルダ(省略するとソースの assets)
//   --json-out <ファイル>   結果を JSON で書く(CI や AI が読む用。人向けの出力はそのまま画面に出る)
//
// 終了コード(CI がこれで成否を判断する):
//   0 … 成功 / 1 … コマンドが失敗した(問題が見つかった、など) / 2 … 使い方の誤り(知らないコマンドやオプション、壊れた JSON)
//
// 結果の JSON を画面(標準出力)ではなくファイルに書くのは、エンジンのログ(printf)も画面に出るため。
// ログの仕組みを作り、ログを別の出力(標準エラー)に分けたら、画面にも JSON を出せるようにする。
//
// CLI で使えるのは、動いているゲームが要らない(ファイルだけを相手にする)コマンドだけ。今は asset.refresh。
// シーンの保存や Play / Stop は、動いているゲームのシーンが相手なので、CLI には登録しない。
//
// 動作スレッド: メインスレッド(= ゲームスレッド)のみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"

#include <functional>
#include <string>
#include <vector>

namespace husky::tools
{
    // 終了コード。
    namespace CliExitCode
    {
        constexpr int Success = 0;
        constexpr int CommandFailed = 1;
        constexpr int UsageError = 2;
    }

    // CLI を1回実行する。画面もウィンドウも使わない(テストから呼べる)。
    // args: コマンドラインの引数(exe の名前は含めない)。
    // defaultAssetsDirectory: --assets が無いときのアセットのフォルダ。
    // out: 人が読む出力(呼んだ側が画面に出す)。コマンドのログは、これとは別に printf で直接出る。
    // 戻り値: 終了コード(CliExitCode)。
    // setupApplication: Application を作った直後に呼ぶ関数(空でもよい)。ゲームの型の登録(game::RegisterGameTypes)を渡す。
    //   tools はゲームを知らない(ゲームがエンジンを使う向きだけにする)ので、CLI の exe から関数として受け取る。
    int RunEditorCli(const std::vector<std::string>& args, const std::string& defaultAssetsDirectory, std::string& out,
                     const std::function<void(engine::Application&)>& setupApplication = {});
}

#endif
