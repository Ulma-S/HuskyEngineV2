// 【開発ビルドだけ】ゲームのコード(モジュール)のエディタのコマンドと、DLL の見張り(HR-4、HR-5)。
//   game.reload: ビルドし直した HuskyGame.dll に、エディタを動かしたまま差し替える(ホットリロード)。編集中だけ。
//   game.autoReload: DLL がビルドし直されたら自動で game.reload する、を切り替える(既定はオン)。
#pragma once

#if HUSKY_DEVELOPMENT

#include "platform/FileTimestamp.h"
#include "tools/EditorCommands.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace husky::tools
{
    class EditorPlayMode;

    // 動作スレッド: ゲームスレッド。playMode はコマンドより長く生きること(編集中かを聞くのに使う)。
    EditorCommand MakeGameReloadCommand(const EditorPlayMode& playMode);

    // DLL のファイルの更新日時から「読み直すか」を決める(HR-5)。時刻は外から渡すので、テストで決まった結果になる。
    //   - 見るのは CheckIntervalSeconds ごと(毎フレームではない)
    //   - 日時が変わっても、すぐには読まない。同じ新しい日時が SettleSeconds 続いたら(リンカが書き終わったら)読む
    //   - 再生中なら読まず、1回だけ WaitForEdit を返す。編集に戻ったら Reload を返す
    class DllChangeDetector
    {
    public:
        static constexpr double CheckIntervalSeconds = 0.5;
        static constexpr double SettleSeconds = 0.5;

        enum class Action
        {
            None,
            Reload,      // 読み直す
            WaitForEdit, // 変わったが再生中(Stop した後に読み直す、と知らせる)
        };

        // 今読んでいる DLL の元のファイルの日時を覚え直す(読んだとき・読み直したとき)。
        void Reset(std::int64_t loadedTime);
        // now 秒の時点で、ファイルを見る番か(見る番なら、次の番は CheckIntervalSeconds 後)。
        bool IsCheckDue(double now);
        // 見た結果を渡す。readOk が false ならファイルが無い・読めない(書いている途中など)。
        Action Update(double now, bool readOk, std::int64_t time, bool editing);

    private:
        std::int64_t m_loadedTime = 0;
        std::int64_t m_pendingTime = 0; // 変わった後の日時(落ち着くのを待っている)
        double m_pendingSince = 0.0;
        double m_nextCheck = 0.0;
        bool m_hasPending = false;
        bool m_announced = false; // WaitForEdit を知らせたか
    };

    // ゲームの DLL を見張り、ビルドし直されたら game.reload を実行する(HR-5)。開発用の UI の毎フレームから Update を呼ぶ。
    class GameModuleAutoReload
    {
    public:
        void RegisterCommands(EditorCommandRegistry& commands); // game.autoReload
        void Update(engine::Application& app, EditorCommandRegistry& commands, const EditorPlayMode& playMode);
        bool IsEnabled() const { return m_enabled; }

    private:
        platform::FileTimestamp m_file; // 元の HuskyGame.dll(パスは読んだときに1回だけ渡す。毎回の確保なし)
        std::string m_loadedPath;       // 最後に見た「読んでいる写し」のパス(変わったら、読み直されたということ)
        DllChangeDetector m_detector;
        std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
        bool m_enabled = true;
    };
}

#endif
