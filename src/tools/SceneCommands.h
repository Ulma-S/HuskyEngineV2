// 【開発ビルドだけ】入れ物のシーンのエディタのコマンド(SC-4、2026-11-14)。
//   scene.open        … シーンをルートシーンとして開く(ワールドを空にする。保存していない変更は消える。Undo の履歴も空にする)
//   scene.load        … 子のシーンを読む(親を省けばルートシーンの下)
//   scene.unload      … 子のシーンを下ろす(ルートシーンは下ろせない)
//   scene.addChild    … ルートシーンの子のシーンの記録を足す(開いたときに一緒に読むかの印つき。Undo できる)
//   scene.removeChild … ルートシーンの子のシーンの記録を外す(Undo できる)
//   scene.list        … シーンの木と、それぞれのアクターの数
// シーンは名前(シーンの一覧の名前)で指す。scene.open と scene.addChild は、GUID か assets の中のパスも受け付ける。
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

namespace husky::tools
{
    // history: scene.open が Undo の履歴を空にするため(開いた後は、前のシーンのアクターを指す履歴が使えない)。
    // history はコマンドより長く生きること。
    EditorCommand MakeOpenSceneCommand(EditorCommandRegistry& history);
    EditorCommand MakeLoadSceneCommand();
    EditorCommand MakeUnloadSceneCommand();
    EditorCommand MakeAddChildSceneCommand();
    EditorCommand MakeRemoveChildSceneCommand();
    EditorCommand MakeListScenesCommand();
}

#endif
