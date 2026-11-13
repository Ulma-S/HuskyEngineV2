// 【開発ビルドだけ】音のエディタのコマンド。
//   audio.testTone: 試しの音(440Hz のサイン波、0.3 秒)を鳴らす。音の出す先が動いているかを耳で確かめる用。
//   audio.play: 音のアセット(.wav)を鳴らす(GUID か assets の中のパス、loop、volume)。試し聞きと、AI からの確認用。
//   audio.stopAll: 鳴っている音をすべて止める。
#pragma once

#if HUSKY_DEVELOPMENT

#include "tools/EditorCommands.h"

namespace husky::tools
{
    // 動作スレッド: ゲームスレッド
    EditorCommand MakeAudioTestToneCommand();
    EditorCommand MakeAudioPlayCommand();
    EditorCommand MakeAudioStopAllCommand();
}

#endif
