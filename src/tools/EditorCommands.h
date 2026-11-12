// エディタのコマンド: エディタの操作(シーンを保存する、など)を、画面から切り離した「名前と引数を持つ操作」として表す。
// 製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// なぜ画面(ImGui)の中に直接書かないか(設計メモ「AIから使いやすいエディタ」):
//   人がボタンやキーで使うときも、将来コマンドラインツールや MCP(AI)から使うときも、同じコマンドを名前で呼ぶ。
//   入口がいくつあっても、操作そのものは1か所にあるので、決まり(GUID を振る、2か所に書く、など)が必ず守られる。
//   将来の Undo も、コマンドの単位で作る。
//
// 1つのコマンドは、機械向けの部分と人向けの部分を両方持つ:
//   - name(機械向け): "scene.save" のような、変えない名前。CLI や AI はこれで呼ぶ。
//   - label、description、shortcut(人向け): 画面に出す名前、何をするかの説明(マウスを乗せると出る。AI への説明にも使う)、
//     ショートカットのキー。
//   - 結果(EditorCommandResult)も、人が読む1行(message)と、細かい内容(details)に分けて返す。
//
// 使い方(main.cpp):
//   tools::EditorCommandRegistry commands;
//   commands.Register(tools::MakeSaveSceneCommand({sourcePath, exePath}));
//   commands.Execute("scene.save", app);          // 名前で呼ぶ(ボタン、CLI、AI も同じ)
//   commands.HandleShortcuts(app);                // 毎フレーム: ショートカットのキーが押されたコマンドを実行する
//
// 動作スレッド: ゲームスレッドのみ(コマンドはシーンや GC 管理のオブジェクトを触るため)。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/ArchiveValue.h"
#include "engine/Input.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace husky::tools
{
    // コマンドを実行した結果。
    struct EditorCommandResult
    {
        bool succeeded = false;
        std::string message;              // 人が読む1行の要約(画面とログに出す)。"Saved scene (1001 actors) to 2 files" など
        std::vector<std::string> details; // 細かい内容(書いたファイルの一覧、失敗の理由など)。無くてもよい

        static EditorCommandResult Success(std::string message);
        static EditorCommandResult Failure(std::string message);
    };

    // コマンドを実行するときに渡すもの。
    struct EditorCommandContext
    {
        engine::Application& app;
        // 引数。キーと値の組(ArchiveValue の Object)か、引数が無ければ空(Null)。
        // ArchiveValue は JSON と同じ形の値の木なので、CLI や AI から JSON で渡された引数を、そのまま渡せる。
        const engine::ArchiveValue& args;
    };

    // コマンド1つ。
    struct EditorCommand
    {
        std::string name;        // 機械向けの名前(一覧の中で重ならない。一度決めたら変えない)。"scene.save" のように「対象.動作」
        std::string label;       // 人向けの名前(画面に出す。英語。日本語のフォントを入れるまで)
        std::string description; // 何をするか(人へのツールチップ、AI への説明)
        engine::Key shortcut = engine::Key::Unknown; // ショートカットのキー。Unknown なら無し
        // 実行する関数。std::function は C# の Func<> にあたる(ラムダ式で、必要なもの(保存先など)を持たせて書ける)。
        std::function<EditorCommandResult(EditorCommandContext&)> execute;
    };

    // コマンドの一覧。登録した順に並べる(画面にもこの順に出す)。
    class EditorCommandRegistry
    {
    public:
        // command を一覧に加える。名前が空、同じ名前が登録済み、execute が空なら、加えずに false。
        bool Register(EditorCommand command);

        // 名前で探す。無ければ nullptr。
        const EditorCommand* Find(const std::string& name) const;

        const std::vector<EditorCommand>& GetCommands() const { return m_commands; }

        // 名前でコマンドを実行する。見つからなければ、失敗の結果を返す(落ちない)。
        // 結果はログに出し、「最後の結果」として覚える(画面に出すため)。
        // args: 引数(既定は空)。
        EditorCommandResult Execute(const std::string& name, engine::Application& app,
                                    const engine::ArchiveValue& args = engine::ArchiveValue());

        // ショートカットのキーが、このフレームで押されたコマンドを実行する(引数は空)。毎フレーム呼ぶ。
        // ImGui が文字の入力にキーボードを使っている間は、Input にキーが届かないので、実行されない。
        // 戻り値: 実行した数。
        std::size_t HandleShortcuts(engine::Application& app);

        // 最後に実行したコマンドの名前と結果。まだ何も実行していなければ、名前は空。
        const std::string& GetLastCommandName() const { return m_lastCommandName; }
        const EditorCommandResult& GetLastResult() const { return m_lastResult; }

    private:
        std::vector<EditorCommand> m_commands;
        std::string m_lastCommandName;
        EditorCommandResult m_lastResult;
    };

    // 「シーンを保存する」コマンド(scene.save、F5)を作る。今のシーンを、paths のすべてに同じ内容で書く。
    // ふつうは「ソースの assets」(残すため)と「exe の隣の assets」(ビルドし直さずに次の起動で読むため)の2か所。
    // どれか1つでも書けなければ、失敗の結果(書けなかったパスを details に)を返す。
    EditorCommand MakeSaveSceneCommand(std::vector<std::string> paths);

    // 「アセットを最新にする」コマンド(asset.refresh)を作る。Unity の Refresh にあたる。assetsDirectory の中で、順に:
    //   1. .meta が無いアセットに、GUID を振って .meta を作る(CreateMissingMetaFiles)
    //   2. スプライトシートを整える(ID を振り、アニメーションのコマの名前を ID に直して書き戻す。NormalizeSpriteSheetsInFolder)
    //   3. アセットの表(GUID → パス)を作り直す(AssetDatabase::Scan)
    // 起動時にも、このコマンドを呼ぶ。ゲームを動かしたまま assets にファイルを足したときは、これを実行すれば
    // インスペクタから選べるようになる。すでに読み込んだアセットの中身は読み直さない。
    // 表の問題(.meta が読めない、対になるアセットが無い、GUID が重なる)があれば、表は作ったうえで、失敗の結果を返す
    // (人に直してもらう必要があるため。ファイルの名前はログに出ている)。
    EditorCommand MakeRefreshAssetsCommand(std::string assetsDirectory);
}

#endif
