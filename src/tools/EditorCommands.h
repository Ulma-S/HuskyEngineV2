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
    // コマンドの呼び出し1つ(名前と引数)。Undo の履歴に「戻すときに呼ぶもの」「やり直すときに呼ぶもの」として入れる。
    // 戻すことも「コマンドを別の引数で呼ぶ」だけなので、どの入口(画面、CLI、AI)から変えた値も、同じやり方で戻せる。
    struct EditorCommandCall
    {
        std::string name;          // コマンドの名前。空なら「呼び出し無し」
        engine::ArchiveValue args; // 引数(Object か Null)

        bool IsEmpty() const { return name.empty(); }
    };

    // コマンドを実行した結果。
    struct EditorCommandResult
    {
        bool succeeded = false;
        std::string message;              // 人が読む1行の要約(画面とログに出す)。"Saved scene (1001 actors) to 2 files" など
        std::vector<std::string> details; // 細かい内容(書いたファイルの一覧、失敗の理由など)。無くてもよい
        // 機械が読むための結果(無ければ Null)。actor.list のアクターの一覧など。CLI の --json-out では "data" に書く。
        // 人向けの details は読みやすさのために途中で切ることがあるが、data にはすべて入れる。
        engine::ArchiveValue data;
        // 元に戻せるコマンドは、成功したときに、戻す呼び出し(undo)とやり直す呼び出し(redo)を書く。
        // 両方が書いてあれば、EditorCommandRegistry が Undo の履歴に入れる(空なら入れない: 読むだけのコマンドなど)。
        // redo を元の引数のままにしないのは、名前で指したアクターの名前が変わっても、guid で確実に同じものを指すため。
        EditorCommandCall undo;
        EditorCommandCall redo;

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
        Any,    // どの種類でもよい(null も可。property.set の value のように、ほかの引数で種類が決まるもの。中身はコマンドが確かめる)
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
        // true なら、ショートカットは Ctrl と一緒に押す(Ctrl+Z など)。false なら、Ctrl を押していないときだけ反応する
        // (Ctrl+Z を押したときに、Z だけのショートカットまで動かないように)。左右どちらの Ctrl でもよい。
        bool shortcutCtrl = false;
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

        // ---- Undo / Redo(設計メモ「Undo はコマンドの単位」)----
        // Execute で成功し、結果に undo と redo があったコマンドを、履歴に積む。新しく積むと、やり直しの履歴は消える
        // (戻した後に別の変更をしたら、戻す前の未来には行けない。ふつうのエディタと同じ)。
        // ふつうは editor.undo / editor.redo のコマンド(MakeUndoCommand / MakeRedoCommand)から呼ぶ。

        // 最後の変更を戻す(履歴の undo を実行する)。戻すものが無ければ失敗。
        //   - 戻すコマンドが今は使えない(再生中など)なら、履歴はそのままで失敗にする(後で戻せる)
        //   - 戻すコマンドが失敗した(アクターが消えたなど)なら、その1件を履歴から捨てて失敗にする(ずっと詰まらないように)
        // この関数自体はログに出さず、「最後の結果」も変えない(editor.undo の Execute が出す)。
        EditorCommandResult Undo(engine::Application& app);
        // 最後に戻した変更を、やり直す(履歴の redo を実行する)。決まりは Undo と同じ。
        EditorCommandResult Redo(engine::Application& app);

        std::size_t GetUndoCount() const { return m_undoHistory.size(); }
        std::size_t GetRedoCount() const { return m_redoHistory.size(); }
        // 次に戻す・やり直すものの説明(画面のツールチップに使う)。無ければ nullptr。
        const char* PeekUndoLabel() const { return m_undoHistory.empty() ? nullptr : m_undoHistory.back().label.c_str(); }
        const char* PeekRedoLabel() const { return m_redoHistory.empty() ? nullptr : m_redoHistory.back().label.c_str(); }
        // 次の Undo / Redo が今は使えない理由(使えるなら nullptr)。editor.undo / editor.redo の whyUnavailable に使う。
        const char* WhyCannotUndo(engine::Application& app) const;
        const char* WhyCannotRedo(engine::Application& app) const;
        // 履歴を空にする(シーンを丸ごと読み直して、履歴の相手がいなくなったときなど)。
        void ClearHistory();

        // 履歴に残す数の上限。超えたら古いものから捨てる。
        static constexpr std::size_t MaxHistory = 100;

    private:
        // 履歴の1件: 何をしたか(人向け)と、戻す呼び出し、やり直す呼び出し。
        struct HistoryEntry
        {
            std::string label; // 元のコマンドの結果の1行("Set Player (PlayerActor).speed = 3" など)
            EditorCommandCall undo;
            EditorCommandCall redo;
        };

        // Execute の中身。record: 結果の undo / redo を履歴に積むか。report: ログに出し、「最後の結果」として覚えるか。
        EditorCommandResult Run(const std::string& name, engine::Application& app, const engine::ArchiveValue& args,
                                bool record, bool report);
        // Undo と Redo の共通部分。from の最後の1件の call(undo か redo)を実行し、成功したら to へ移す。
        EditorCommandResult Replay(engine::Application& app, std::vector<HistoryEntry>& from,
                                   std::vector<HistoryEntry>& to, bool isUndo);
        const char* WhyCannotReplay(engine::Application& app, const std::vector<HistoryEntry>& history,
                                    bool isUndo) const;

        std::vector<EditorCommand> m_commands;
        std::string m_lastCommandName;
        EditorCommandResult m_lastResult;
        bool m_logResults = true;
        std::vector<HistoryEntry> m_undoHistory; // 戻せる変更(後ろほど新しい)
        std::vector<HistoryEntry> m_redoHistory; // 戻した変更(後ろほど最近戻したもの)
    };

    // 「元に戻す」コマンド(editor.undo)と「やり直す」コマンド(editor.redo)を作る。commands の履歴を使うので、
    // commands はコマンドより長く生きていること(ふつうは commands 自身に登録する)。ショートカットは Ctrl+Z と Ctrl+Y。
    EditorCommand MakeUndoCommand(EditorCommandRegistry& commands);
    EditorCommand MakeRedoCommand(EditorCommandRegistry& commands);

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

    // "log.catalog": ログのカタログ(登録した全部のチャンネルとメッセージの定義の一覧)を JSON のファイルに書く(LOG-3)。
    // 将来のビュワーは、ログのファイル(番号と値)をこれで文にする。引数 path(省略すると exe の隣の logs/catalog.json)。
    // 載るのは、この exe に入っているチャンネルだけ。ファイルを書くだけで、シーンもアセットも変えない(Undo なし)。
    // 中身は tools/LogCatalog.cpp。
    EditorCommand MakeLogCatalogCommand();

    // カタログの値の木を作る(コマンドとテストが使う)。
    engine::ArchiveValue BuildLogCatalog();

    // 「アクターを置く」コマンド(actor.spawn)を作る。引数は prefab か type のどちらか一方と、x、y(位置。省略すると 0):
    //   - prefab(GUID か assets の中のパス): プレハブのとおりにアクター、コンポーネント、値を作り、元のプレハブを覚えさせる
    //     (Actor::prefab)。読み込み中なら、読み終わるまで待つ。
    //   - type(型の名前。Actor の子孫で、型の登録簿にあるもの): アクターだけを作る(コンポーネントは付かない)。
    // 結果の data に、置いたアクターの guid を入れる。シーンを書き換えるコマンド(modifiesScene)。
    // Undo は actor.destroy、Redo は置いた直後の控えでの actor.restore(同じ guid に戻るので、後の履歴と食い違わない)。
    EditorCommand MakeSpawnActorCommand();

    // 「プレハブを作る」コマンド(prefab.create)を作る。引数: actor(名前か guid)、path(assets の中の、.prefab で終わるパス)。
    // アクターと、そのコンポーネントと値を Prefab::Write で値の木にし、assetsDirectory の下に JSON で書き、新しい GUID の .meta も書く。
    // その後アセットの表を作り直すので、すぐに actor.spawn の prefab で指せる。すでにあるファイルは上書きしない。
    // 元のアクターはそのまま(プレハブにつながりもしない)。シーンは読むだけ(readsScene)。ファイルを書くので Undo はできない。
    EditorCommand MakeCreatePrefabCommand(std::string assetsDirectory);

    // 引数で書かれたアクター(名前か guid の文字列)を、シーンから探す。
    //   - guid の形の文字列なら guid で探す(名前より先に見る)
    //   - そうでなければ名前で探す。1体だけなら、それを返す
    // 見つからない、または名前が複数のアクターに当てはまるときは nullptr を返し、error に人が読める理由を書く
    // (複数なら、候補を guid、型、位置つきで並べる。黙ってどれかを選ばない)。
    engine::Actor* FindActorFromArg(engine::World& scene, const std::string& actorText, std::string& error);

    // アクターを人に見せる文字("Player (PlayerActor)"。名前が無ければ "PlayerActor at (12, 34)")。
    std::string DescribeActor(const engine::Actor& actor);

    // 参照の相手(ObjectHandle / ObjectRef の中身)を、コマンドの引数の形にする(REF-1)。
    //   - シーンのアクター → guid の文字列
    //   - シーンのアクターのコンポーネント → {"actor": guid, "component": 型の名前}(同じ型の2つ目からは "index": 何個目か)
    //   - 何も指さない・もう無い・シーンの外のもの → null
    // property.set の value と、Undo の前の値に使う(ハンドルはその時のメモリでしか意味が無いので、guid で指す)。
    engine::ArchiveValue MakeReferenceArg(engine::Application& app, core::ObjectHandle handle);

    // MakeReferenceArg の逆。アクターは名前でも指せる(FindActorFromArg)。null なら、どこも指さないハンドル。
    // 見つからなければ false と、人が読める理由。
    bool FindReferenceFromArg(engine::Application& app, const engine::ArchiveValue& value, core::ObjectHandle& out,
                              std::string& error);

    // 参照の配列(std::vector<ObjectRef<T>>)のプロパティか。
    bool IsReferenceArray(const core::Property& property);

    // 参照の配列の中身を、MakeReferenceArg の形の配列にする(container はプロパティの持ち主の先頭)。
    engine::ArchiveValue MakeReferenceArrayArg(engine::Application& app, const core::Property& property,
                                               const void* container);

    // MakeReferenceArrayArg の形の配列で、参照の配列をまるごと入れ替える。要素ごとに相手を引き、ObjectRef<T> の T で
    // 型を確かめる。1つでも見つからない・型が違えば、何も変えずに false と理由。
    bool AssignReferenceArray(engine::Application& app, const core::Property& property, void* container,
                              const engine::ArchiveValue& value, std::string& error);

    // 「アクターの一覧」コマンド(actor.list)を作る。シーンのアクターを、名前、型、guid、位置で並べる(読むだけ)。
    // 引数: name、type(省略可。名前が同じもの、その型か子孫のものだけに絞る)。data に全件を入れる。
    EditorCommand MakeListActorsCommand();

    // 「プロパティを書き換える」コマンド(property.set)を作る。引数: actor(名前か guid)、
    // component(省略可。コンポーネントの型の名前。書けば、アクターのその型(か子孫)の最初のコンポーネントを書き換える)、
    // property(プロパティの名前。構造体の中は "transform.position.x" のように点でつなぐ)、
    // value(新しい値。種類はプロパティに合わせる。構造体そのものなら {"r": 1, "g": 0} のような形で、書いた値だけが変わる)。
    // 書き換えられるのは bool / int / float / string、構造体、アセットの参照(GUID の文字列か null。guid、ハンドル、配列は変えられない)。
    // 値の確かめは、シーンの読み込みと同じ道具(ObjectSerializer)で行う。成功すると、前の値に戻す undo を返す(Undo できる)。
    EditorCommand MakeSetPropertyCommand();

    // 「まとめて書き換える」コマンド(property.setMany)を作る。引数: actor(名前か guid)、
    // values(property.set の引数と同じ形 {"component", "property", "value"} の配列。component は省略可)。
    // 1つでも書けなければ、何も変えずに失敗する。Undo は、前の値での property.setMany(prefab.revert の Undo にも使う)。
    EditorCommand MakeSetManyPropertiesCommand();

    // 「アクターを消す」コマンド(actor.destroy)を作る。引数: actor(名前か guid)。アクターに Destroy の印を付ける
    // (フレームの終わりに、コンポーネントと一緒に消える)。消す前に、シーンのファイルと同じ形でアクター1体を控え、
    // Undo は、その控えでの actor.restore(同じ guid で作り直す)。ほかのアクターからの参照(カメラの追う相手など)は、
    // 戻してもつながり直らない(今の制限)。
    EditorCommand MakeDestroyActorCommand();
    // actor.setParent(HIER-3): アクターの親を付け替える(actor、parent(省くか "" で親なし)、keepWorld(既定 true)、
    // transform(付け替えた後のローカルの値。Undo / Redo が使う))。輪になる・Rigidbody2D を持つときは断る。Undo できる。
    EditorCommand MakeSetParentCommand();

    // 「アクターを戻す」コマンド(actor.restore)を作る。引数: data(actor.destroy が控えた、アクター1体のシーンの形)。
    // シーンを読むときと同じ道具(SceneSerializer::Read)で、同じ guid のアクターを一覧の末尾に作る。プレハブから置いたものは、
    // プレハブから作ってから違いを重ねる。同じ guid のアクターが生きていれば断る。ふつうは editor.undo から呼ばれる。
    EditorCommand MakeRestoreActorCommand();

    // 「コンポーネントを足す」コマンド(component.add)を作る(PF-7c)。引数: actor、type(Component の子孫の型の名前)、
    // index(省略可。一覧の何番目に入れるか。省くと末尾)、data(省略可。component.remove が控えた値。Undo で使う)。
    // data があれば、その値(prefabComponentId も)を書く。Undo は、入れた位置の component.remove。
    EditorCommand MakeAddComponentCommand();

    // 「コンポーネントを外す」コマンド(component.remove)を作る(PF-7c)。引数: actor、component(型の名前。その型の最初のもの)、
    // index(省略可。一覧の何番目かで指す。Undo / Redo が使う。同じ型が2つあっても取り違えないため)。
    // 外す前に値を控え、Undo は「同じ型・同じ位置・控えた値」の component.add。アクターを指すハンドルは guid で控えるので、
    // 戻すとつながり直る(相手が生きていれば)。アクター以外を指すハンドルは戻すと null(警告を出す)。
    EditorCommand MakeRemoveComponentCommand();

    // 「外したコンポーネントをプレハブに戻す」コマンド(prefab.revertComponent)を作る(PF-7d)。引数: actor、componentId
    // (プレハブの中の ID。Prefab::FindOverrides の removedComponents)。プレハブの値と ID で作り直し、プレハブで1つ前にある
    // コンポーネントのすぐ後ろに入れる(中身は component.add と同じ)。Undo は component.remove。
    // 足したコンポーネントの Revert は、component.remove でそのまま外せばよい。
    EditorCommand MakeRevertRemovedComponentCommand();

    // 「コンポーネントの出し入れをプレハブに書き戻す」コマンド(prefab.applyComponent)を作る(PF-7e)。引数: actor と、
    // added(置いた後に足したコンポーネントの型の名前。プレハブに新しい ID で足す)か removed(外したもののプレハブの中の ID。
    // プレハブから消す)のどちらか一方。プレハブのファイル(assetsDirectory の中)を書き直して読み直し、同じプレハブから置いた
    // 全員に伝える(足したときは全員に足し、消したときは全員から外す)。Undo できない。
    EditorCommand MakeApplyComponentCommand(std::string assetsDirectory);

    // 子のアクターの出し入れ(HIER)。prefab.revertChild: 置いた後に消した子(actor と child = プレハブの中の ID)を、
    // プレハブの値で戻す(Undo できる)。prefab.applyChild: 足した子(added = guid)をプレハブに書き込むか、消した子(removed = ID)を
    // プレハブから消し、同じプレハブから置いた全員に伝える(Undo できない)。
    EditorCommand MakeRevertChildCommand();
    EditorCommand MakeApplyChildCommand(std::string assetsDirectory);

    // 「プレハブとの違いの一覧」コマンド(prefab.overrides)を作る。引数: actor。読むだけ。
    // data に {component, property, value, prefabValue, perInstance} の配列を入れる(Prefab::FindOverrides)。
    EditorCommand MakeListOverridesCommand();

    // 「プレハブに戻す」コマンド(prefab.revert)を作る。引数: actor、component、property(どちらも省略可)。
    // property を指せばその値(構造体ならその中全部)を、省けば名前・位置・回転を除く全部の違いを、プレハブの値に戻す。
    // 戻した値は違いではなくなるので、プレハブを直すと伝わるようになる。Undo できる。
    EditorCommand MakeRevertPrefabCommand();

    // 「プレハブに書き戻す」コマンド(prefab.apply)を作る。引数は prefab.revert と同じ(actor、component、property)。
    // 選んだ違いを、assetsDirectory の下のプレハブのファイルに書き(Prefab::WriteChanges)、新しいアセットとして読み直し
    // (AssetLoader::ReloadPrefab)、同じプレハブから置いた全員に伝える(Prefab::FollowChanges。自分で値を変えたものはそのまま)。
    // プレハブのファイルを書き換えるので、Undo はできない。
    EditorCommand MakeApplyPrefabCommand(std::string assetsDirectory);

    // 「データのアセットを作る」コマンド(data.create。SD-3a)を作る。引数は type(ScriptableData の子の型の名前)と
    // path(assets の中、.sdata で終わる)。型の既定の値で .sdata と .meta を assetsDirectory の下に書き、表を作り直す。
    // 上書きしない。Undo はできない(ファイルを消す操作はまだ作らない)。結果の data に guid と path。
    EditorCommand MakeCreateDataCommand(std::string assetsDirectory);

    // 「データの値を1つ変える」コマンド(data.set。SD-3a)を作る。引数は asset(GUID かパス)、property(点でつないだ道筋)、value。
    // 読み込んだ中身は読むだけなので書き換えず、写しに値を入れてファイルに書き、新しいアセットとして読み直す
    // (AssetLoader::ReloadScriptableData。使っている AssetRef は次の Resolve で乗り換える)。
    // Undo は前の値での data.set。
    EditorCommand MakeSetDataCommand();
}

#endif
