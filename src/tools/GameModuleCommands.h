// 【開発ビルドだけ】ゲームのコード(モジュール)のエディタのコマンド(HR-4)。
//   game.reload: ビルドし直した HuskyGame.dll に、エディタを動かしたまま差し替える(ホットリロード)。編集中だけ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

namespace husky::tools
{
    class EditorPlayMode;

    // 動作スレッド: ゲームスレッド。playMode はコマンドより長く生きること(編集中かを聞くのに使う)。
    EditorCommand MakeGameReloadCommand(const EditorPlayMode& playMode);
}

#endif
