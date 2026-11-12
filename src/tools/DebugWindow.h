// 開発用のデバッグの窓(ImGui)。今の状態(EDIT / PLAYING)、フレームの時間、描画、オブジェクト、メモリの数字と、
// エディタのコマンドのボタンを出す。製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// 引数のあるコマンドは、ボタン(「Spawn actor...」のように ... が付く)を押すと入力欄の小窓が開き、
// 「Run」で実行する。入力欄は、コマンドの引数の形(EditorCommandArg)から自動で作る(インスペクタがリフレクションから
// 画面を作るのと同じ考え方)。新しいコマンドを足しても、画面のコードは要らない。入力した値は、次に開いたときも残る。
//
// 使い方(main.cpp):
//   tools::DebugWindow debugWindow;   // 入力した値を覚えておくので、毎フレーム作り直さない
//   app.SetDevUiCallback([&](engine::Application& a) { debugWindow.Draw(a, &commands, &playMode); });
//
// 窓の文字は、今は英語で書く(ImGui の既定のフォントには日本語が無いため。日本語のフォントを入れるかは後で相談する)。
//
// 動作スレッド: ゲームスレッドのみ(Application::SetDevUiCallback から呼ばれる)。
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/ArchiveValue.h"
#include "tools/EditorCommands.h"
#include "tools/EditorPlayMode.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace husky::tools
{
    // 引数1つ分の入力欄の値。種類ごとに使う場所が違う(String は text、Float は number、など)。
    struct CommandArgInput
    {
        std::string text;     // String(選択肢から選んだときも、ここに入る)
        double number = 0.0;  // Float
        int integer = 0;      // Int
        bool flag = false;    // Bool
        // 省略できる引数を、実行のときに渡すか。既定値があるもの、または人が値を変えたものは渡す。
        bool send = false;
    };

    class DebugWindow
    {
    public:
        // 窓を作る。毎フレーム、開発用の UI のフレームの中で呼ぶ。
        // commands: 「Editor commands」の欄に、登録されたコマンドをボタンで並べ(使えないものは灰色)、最後に実行した結果を出す。
        //           ボタンは名前でコマンドを呼ぶだけ(何をするかは、コマンドの側にある)。nullptr なら、欄を出さない。
        // playMode: 一番上に、今の状態(EDIT / PLAYING / PAUSED)を出す。nullptr なら出さない。
        void Draw(engine::Application& app, EditorCommandRegistry* commands, const EditorPlayMode* playMode = nullptr);

        // 次の Draw で、command の入力欄の小窓を開く(ボタンを押したのと同じ。テストや、ほかの道具から使う)。
        void OpenArgsInput(const std::string& commandName) { m_requestedArgsInput = commandName; }

        // command の入力欄の値(初めてなら、引数の形の既定値で作る)。
        std::vector<CommandArgInput>& GetArgInputs(const EditorCommand& command);

        // 入力欄の値から、コマンドに渡す引数(JSON と同じ形の ArchiveValue のオブジェクト)を作る。
        // 省略できない引数は必ず入れ、省略できる引数は send が true のものだけ入れる。
        static engine::ArchiveValue BuildArgs(const EditorCommand& command, const std::vector<CommandArgInput>& inputs);

    private:
        void DrawCommands(engine::Application& app, EditorCommandRegistry& commands);
        // 入力欄の小窓の中身。Run が押されたら true(引数は outArgs に入る)。
        bool DrawArgsPopup(engine::Application& app, const EditorCommand& command, engine::ArchiveValue& outArgs);

        // コマンドの名前 → 入力欄の値(引数の順)。小窓を閉じても残し、次に開いたときに同じ値から始める。
        std::unordered_map<std::string, std::vector<CommandArgInput>> m_argInputs;
        // ボタンの文字を組み立てる入れ物(毎フレーム作り直すと確保が起きるので、使い回す)。
        std::string m_buttonText;
        // OpenArgsInput で頼まれた小窓(次の Draw で開く)。
        std::string m_requestedArgsInput;
        // 押されたコマンドに渡す引数(毎フレーム作ると確保が起きるので、使い回す)。
        engine::ArchiveValue m_clickedArgs;
    };

    // キーの、人に見せる名前("F5" など)。ショートカットの表示に使う。
    const char* GetKeyDisplayName(engine::Key key);
}

#endif
