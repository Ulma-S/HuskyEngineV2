// 【開発ビルドだけ】音のエディタのコマンド。
//   audio.testTone: 試しの音(440Hz のサイン波、0.3 秒)を鳴らす。音の出す先が動いているかを耳で確かめる用。
#pragma once

#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

namespace husky::tools
{
    // 動作スレッド: ゲームスレッド
    EditorCommand MakeAudioTestToneCommand();
}

#endif
