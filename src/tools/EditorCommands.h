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

#include "engine/Actor.h"
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
        // 機械が読むための結果(無ければ Null)。actor.list のアクターの一覧など。CLI の --json-out では "data" に書く。
        // 人向けの details は読みやすさのために途中で切ることがあるが、data にはすべて入れる。
        engine::ArchiveValue data;

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

    // 引数の値の種類。JSON で書ける基本の値だけ(CLI や AI から JSON で渡すため)。
    enum class EditorArgType
    {
        Bool,   // true / false
        Int,    // 整数
        Float,  // 数(整数で書いてもよい: 100 と 100.0 は同じ)
        String, // 文字列
        Any,    // どの種類でもよい(property.set の value のように、ほかの引数で種類が決まるもの。中身はコマンドが確かめる)
    };

    // 画面や説明に出す、種類の名前("float" など)。
    const char* GetArgTypeName(EditorArgType type);

    // 選べる値1つ。value は引数として渡す値(アクターなら guid の文字列)、label は人に見せる文字("Player (PlayerActor)")。
    // 人は label を見て選び、裏では value が渡る(人は GUID を意識しなくてよい)。label が空なら value をそのまま見せる。
    struct EditorArgChoice
    {
        std::string value;
        std::string label;
    };

    // 引数1つの形。コマンドが「どんな引数を受け取るか」を、実行する前に分かるように書いておく。
    // Execute が、実行の前にこの形で確かめる(足りない、種類が違う、知らない名前なら、実行せずに理由つきで失敗)。
    // --list の説明や、AI に渡すツールの説明(どう呼べばよいか)にも、そのまま使う。
    struct EditorCommandArg
    {
        std::string name;        // JSON のキー("x" など)
        EditorArgType type = EditorArgType::String;
        std::string description; // 何を渡すか(人と AI への説明)
        bool required = true;    // false なら省略できる(省略したときの値は、description に書く)
        // 省略したときの値(無ければ Null)。画面の入力欄の最初の値と、--list の説明に使う。
        // コマンドの中でも、省略されたらこの値を使うようにそろえる。
        engine::ArchiveValue defaultValue;
        // 選べる値の一覧を返す関数(無ければ空の関数。自由に入力する)。画面では選択肢の箱になり、--list にも並ぶ。
        // 「登録されているアクターの型」のように、そのときのアプリの中身で決まるので、関数で受け取る。
        // args は、今入力されているほかの引数(「選んだアクターのプロパティ」のように、ほかの引数で一覧が変わるもののため)。
        // 一覧の外の値も Execute は断らない(コマンドの中で確かめる)。
        std::function<std::vector<EditorArgChoice>(engine::Application&, const engine::ArchiveValue& args)> choices;
    };

    // コマンド1つ。
    struct EditorCommand
    {
        std::string name;        // 機械向けの名前(一覧の中で重ならない。一度決めたら変えない)。"scene.save" のように「対象.動作」
        std::string label;       // 人向けの名前(画面に出す。英語。日本語のフォントを入れるまで)
        std::string description; // 何をするか(人へのツールチップ、AI への説明)
        engine::Key shortcut = engine::Key::Unknown; // ショートカットのキー。Unknown なら無し
        // 画面でボタンを並べるときのまとまり。登録の順で前のコマンドと同じ group なら、同じ行に並べる("play" の4つなど)。
        std::string group;
        // 実行する関数。std::function は C# の Func<> にあたる(ラムダ式で、必要なもの(保存先など)を持たせて書ける)。
        std::function<EditorCommandResult(EditorCommandContext&)> execute;
        // 今は使えない理由を返す関数(使えるなら nullptr)。空の関数なら、いつでも使える。
        // 理由は文字列の定数("Stop playing first" など)で返す(画面が毎フレーム聞くので、std::string を作って確保しないように)。
        // 「再生中はシーンを保存できない」のように、状態によって使えないコマンドに付ける。
        // 画面ではボタンを灰色にして、マウスを乗せると理由を出す。CLI や AI から呼んだときは、理由つきの失敗になる。
        std::function<const char*(engine::Application&)> whyUnavailable;
        // 受け取る引数の形(無ければ空)。
        std::vector<EditorCommandArg> args;
        // シーンを書き換えるコマンドなら true。CLI は、シーンのファイル(--scene)を読んでから実行し、成功したら保存する。
        bool modifiesScene = false;
        // シーンを読むだけのコマンドなら true(actor.list など)。CLI は --scene のファイルを読んでから実行し、保存はしない。
        bool readsScene = false;

        // 省略できない引数があるか(画面のボタンは、引数を入力する欄ができるまで押せない)。
        bool HasRequiredArgs() const;
    };

    // args が、command の引数の形に合っているかを確かめる。合っていれば空の文字列、違えば人が読める理由
    // ("Missing argument 'type' (string): ..." など)。args は Object か、引数が無ければ Null。
    std::string ValidateCommandArgs(const EditorCommand& command, const engine::ArchiveValue& args);

    // コマンドの一覧。登録した順に並べる(画面にもこの順に出す)。
    class EditorCommandRegistry
    {
    public:
        // command を一覧に加える。名前が空、同じ名前が登録済み、execute が空なら、加えずに false。
        bool Register(EditorCommand command);

        // 名前で探す。無ければ nullptr。
        const EditorCommand* Find(const std::string& name) const;

        const std::vector<EditorCommand>& GetCommands() const { return m_commands; }

        // command が今は使えない理由。使えるなら nullptr。
        static const char* GetUnavailableReason(const EditorCommand& command, engine::Application& app);

        // 名前でコマンドを実行する。見つからない、または今は使えない(whyUnavailable)なら、失敗の結果を返す(落ちない)。
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

        // 実行の結果をログに出すか(既定は出す)。結果を自分で出す入口(CLI)は、二重に出ないように止める。
        void SetLogResults(bool enabled) { m_logResults = enabled; }

    private:
        std::vector<EditorCommand> m_commands;
        std::string m_lastCommandName;
        EditorCommandResult m_lastResult;
        bool m_logResults = true;
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

    // 「アクターを置く」コマンド(actor.spawn)を作る。引数: type(型の名前。Actor の子孫で、型の登録簿にあるもの)、
    // x、y(位置。省略すると 0)。置くのはアクターだけで、見た目などのコンポーネントは付かない
    // (決まった組み合わせで置くのは、将来のプレハブの役目)。シーンを書き換えるコマンド(modifiesScene)。
    EditorCommand MakeSpawnActorCommand();

    // 引数で書かれたアクター(名前か guid の文字列)を、シーンから探す。
    //   - guid の形の文字列なら guid で探す(名前より先に見る)
    //   - そうでなければ名前で探す。1体だけなら、それを返す
    // 見つからない、または名前が複数のアクターに当てはまるときは nullptr を返し、error に人が読める理由を書く
    // (複数なら、候補を guid、型、位置つきで並べる。黙ってどれかを選ばない)。
    engine::Actor* FindActorFromArg(engine::Scene& scene, const std::string& actorText, std::string& error);

    // アクターを人に見せる文字("Player (PlayerActor)"。名前が無ければ "PlayerActor at (12, 34)")。
    std::string DescribeActor(const engine::Actor& actor);

    // 「アクターの一覧」コマンド(actor.list)を作る。シーンのアクターを、名前、型、guid、位置で並べる(読むだけ)。
    // 引数: name、type(省略可。名前が同じもの、その型か子孫のものだけに絞る)。data に全件を入れる。
    EditorCommand MakeListActorsCommand();

    // 「プロパティを書き換える」コマンド(property.set)を作る。引数: actor(名前か guid)、property(プロパティの名前)、
    // value(新しい値。種類はプロパティに合わせる)。今書き換えられるのは、アクター自身の bool / int / float / string の
    // プロパティ(guid は変えられない)。値の確かめは、シーンの読み込みと同じ道具(ObjectSerializer)で行う。
    EditorCommand MakeSetPropertyCommand();
}

#endif
