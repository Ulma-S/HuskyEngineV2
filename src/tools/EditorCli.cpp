// EditorCli.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCli.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/ArchiveValue.h"
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"
#include "tools/EditorCommands.h"

#include <utility> // std::move

namespace husky::tools
{
    namespace
    {
        const char* const HelpText =
            "HuskyEditorCli - run an editor command without opening the editor\n"
            "\n"
            "Usage:\n"
            "  HuskyEditorCli --help                    Show this help\n"
            "  HuskyEditorCli --list                    List the commands you can run\n"
            "  HuskyEditorCli <command> [args-json]     Run a command (args: a JSON object)\n"
            "\n"
            "Options:\n"
            "  --assets <folder>      The assets folder (default: the source assets folder)\n"
            "  --scene <file>         The scene file for commands that change the scene: it is loaded first\n"
            "                         (or started empty if it does not exist) and saved if the command succeeds\n"
            "  --json-out <file>      Also write the result as JSON to <file> (for CI and AI)\n"
            "\n"
            "Exit code: 0 = success, 1 = the command failed, 2 = usage error\n";

        // コマンドラインを読んだ結果。
        struct CliOptions
        {
            bool help = false;
            bool list = false;
            std::string assetsDirectory;
            std::string jsonOutPath;
            std::string scenePath;
            std::string commandName;
            std::string argsJson;
        };

        // 引数を読む。誤りがあれば、理由を error に書いて false。
        bool ParseArgs(const std::vector<std::string>& args, CliOptions& options, std::string& error)
        {
            std::size_t positional = 0; // オプションでない引数の数(1 つ目がコマンドの名前、2 つ目が引数の JSON)
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string& arg = args[i];
                if (arg == "--help" || arg == "-h")
                {
                    options.help = true;
                }
                else if (arg == "--list")
                {
                    options.list = true;
                }
                else if (arg == "--assets" || arg == "--json-out" || arg == "--scene")
                {
                    if (i + 1 >= args.size())
                    {
                        error = arg + " needs a value";
                        return false;
                    }
                    std::string& target = arg == "--assets" ? options.assetsDirectory
                                         : arg == "--scene"  ? options.scenePath
                                                             : options.jsonOutPath;
                    target = args[++i];
                }
                else if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-')
                {
                    error = "Unknown option: " + arg;
                    return false;
                }
                else if (positional == 0)
                {
                    options.commandName = arg;
                    ++positional;
                }
                else if (positional == 1)
                {
                    options.argsJson = arg;
                    ++positional;
                }
                else
                {
                    error = "Too many arguments: " + arg + " (put all arguments in one JSON object)";
                    return false;
                }
            }
            return true;
        }

        // CLI で使えるコマンドを登録する(動いているゲームが要らないものだけ)。
        void RegisterCliCommands(EditorCommandRegistry& commands, const std::string& assetsDirectory)
        {
            commands.Register(MakeRefreshAssetsCommand(assetsDirectory));
            commands.Register(MakeSpawnActorCommand());
            commands.Register(MakeDestroyActorCommand());
            commands.Register(MakeAddComponentCommand());
            commands.Register(MakeRemoveComponentCommand());
            commands.Register(MakeCreatePrefabCommand(assetsDirectory));
            commands.Register(MakeListActorsCommand());
            commands.Register(MakeSetPropertyCommand());
            commands.Register(MakeSetManyPropertiesCommand());
            commands.Register(MakeListOverridesCommand());
            commands.Register(MakeRevertPrefabCommand());
            commands.Register(MakeApplyPrefabCommand(assetsDirectory));
        }

        // value を JSON にして path に書く。書けなければ、理由を out に足して false。
        bool WriteJsonFile(const engine::ArchiveValue& value, const std::string& path, std::string& out)
        {
            std::string text;
            if (!engine::JsonFormat::Write(value, text, true) || !platform::WriteTextFile(path.c_str(), text))
            {
                out += "Could not write the JSON result: " + path + "\n";
                return false;
            }
            return true;
        }
    }

    int RunEditorCli(const std::vector<std::string>& args, const std::string& defaultAssetsDirectory, std::string& out,
                     const std::function<void(engine::Application&)>& setupApplication)
    {
        CliOptions options;
        std::string error;
        if (!ParseArgs(args, options, error))
        {
            out += "Error: " + error + "\n\n" + HelpText;
            return CliExitCode::UsageError;
        }
        if (options.help)
        {
            out += HelpText;
            return CliExitCode::Success;
        }
        if (!options.list && options.commandName.empty())
        {
            out += std::string("Error: no command given\n\n") + HelpText;
            return CliExitCode::UsageError;
        }

        const std::string assetsDirectory =
            options.assetsDirectory.empty() ? defaultAssetsDirectory : options.assetsDirectory;

        // 画面の無いエディタ: ウィンドウも GPU も作らない Application(テストと同じ形)。
        engine::Application app;
        if (setupApplication)
        {
            setupApplication(app); // ゲームの型の登録など
        }
        EditorCommandRegistry commands;
        commands.SetLogResults(false); // 結果は CLI が out に出す(ログと二重に出さない)
        RegisterCliCommands(commands, assetsDirectory);

        // --list: コマンドの名前と説明を並べる。
        if (options.list)
        {
            engine::ArchiveValue list = engine::ArchiveValue::MakeArray();
            for (const EditorCommand& command : commands.GetCommands())
            {
                out += command.name + "  (" + command.label + ")\n    " + command.description + "\n";
                if (command.modifiesScene)
                {
                    out += "    Changes the scene: use --scene <file>\n";
                }
                else if (command.readsScene)
                {
                    out += "    Reads the scene: use --scene <file>\n";
                }
                engine::ArchiveValue& item = list.Append(engine::ArchiveValue::MakeObject());
                item.Set("name", engine::ArchiveValue(command.name));
                item.Set("label", engine::ArchiveValue(command.label));
                item.Set("description", engine::ArchiveValue(command.description));
                item.Set("modifiesScene", engine::ArchiveValue(command.modifiesScene));
                item.Set("readsScene", engine::ArchiveValue(command.readsScene));
                // 引数の形も出す(人には一覧で、AI には JSON で「どう呼べばよいか」が分かるように)。
                engine::ArchiveValue& argList = item.Set("args", engine::ArchiveValue::MakeArray());
                for (const EditorCommandArg& arg : command.args)
                {
                    out += "    " + arg.name + " (" + GetArgTypeName(arg.type) + (arg.required ? ", required" : "") +
                           "): " + arg.description + "\n";
                    engine::ArchiveValue& argItem = argList.Append(engine::ArchiveValue::MakeObject());
                    argItem.Set("name", engine::ArchiveValue(arg.name));
                    argItem.Set("type", engine::ArchiveValue(GetArgTypeName(arg.type)));
                    argItem.Set("required", engine::ArchiveValue(arg.required));
                    argItem.Set("description", engine::ArchiveValue(arg.description));
                    // 既定値(省略したときの値)。人には JSON の書き方のまま見せる。
                    if (!arg.defaultValue.IsNull())
                    {
                        std::string defaultText;
                        engine::JsonFormat::Write(arg.defaultValue, defaultText, false);
                        out += "        default: " + defaultText + "\n";
                        argItem.Set("default", arg.defaultValue);
                    }
                    // 選べる値(ゲームの型を登録した後のアプリで決まる)。AI はここから選べば、打ち間違えない。
                    if (arg.choices)
                    {
                        // シーンを読む前なので、シーンで決まる選択肢(アクターなど)は空になる。空なら書かない。
                        const std::vector<EditorArgChoice> choices = arg.choices(app, engine::ArchiveValue());
                        if (!choices.empty())
                        {
                            engine::ArchiveValue& choiceList = argItem.Set("choices", engine::ArchiveValue::MakeArray());
                            std::string line = "        choices:";
                            for (const EditorArgChoice& choice : choices)
                            {
                                line += " " + choice.value;
                                engine::ArchiveValue& choiceItem = choiceList.Append(engine::ArchiveValue::MakeObject());
                                choiceItem.Set("value", engine::ArchiveValue(choice.value));
                                choiceItem.Set("label", engine::ArchiveValue(choice.label.empty() ? choice.value : choice.label));
                            }
                            out += line + "\n";
                        }
                    }
                }
            }
            if (!options.jsonOutPath.empty())
            {
                engine::ArchiveValue root = engine::ArchiveValue::MakeObject();
                root.Set("commands", std::move(list));
                if (!WriteJsonFile(root, options.jsonOutPath, out))
                {
                    return CliExitCode::CommandFailed;
                }
            }
            return CliExitCode::Success;
        }

        // 知らないコマンドは、使い方の誤り(コマンド自身の失敗とは分ける)。
        const EditorCommand* command = commands.Find(options.commandName);
        if (command == nullptr)
        {
            out += "Error: unknown command: " + options.commandName + " (see --list)\n";
            return CliExitCode::UsageError;
        }
        // シーンを書き換える・読むコマンドには、相手にするシーンのファイルが要る。
        if ((command->modifiesScene || command->readsScene) && options.scenePath.empty())
        {
            out += "Error: " + options.commandName + (command->modifiesScene ? " changes" : " reads") +
                   " the scene, so it needs --scene <file>\n";
            return CliExitCode::UsageError;
        }

        // 引数: JSON のオブジェクト。省略したら空。
        engine::ArchiveValue commandArgs;
        if (!options.argsJson.empty())
        {
            std::string parseError;
            if (!engine::JsonFormat::Read(options.argsJson, commandArgs, &parseError) ||
                commandArgs.GetType() != engine::ArchiveValue::Type::Object)
            {
                out += "Error: the arguments must be a JSON object, like {\"name\": 1}";
                out += parseError.empty() ? "\n" : " (" + parseError + ")\n";
                return CliExitCode::UsageError;
            }
        }

        // 引数の形の誤り(足りない、種類が違う、知らない名前)も、使い方の誤り。実行する前に確かめる。
        if (const std::string argError = ValidateCommandArgs(*command, commandArgs); !argError.empty())
        {
            out += "Error: " + argError + " (see --list)\n";
            return CliExitCode::UsageError;
        }
        // アセットの表(GUID → パス)を、シーンを読む前に作る。シーンのプレハブから置いたアクターは、読むときにプレハブを GUID で探し、
        // actor.spawn の prefab も GUID やパスでアセットを指すため。ファイルは直さない(直すのは asset.refresh の役目)。
        app.GetAssetDatabase().Scan(assetsDirectory);

        // --scene: シーンのファイルを読んでから実行する(無ければ、空のシーンから始めて新しく作る)。
        const bool newScene = !options.scenePath.empty() && !platform::FileExists(options.scenePath.c_str());
        if (newScene && !command->modifiesScene) // 読むだけのコマンドは、無いファイルを作らない
        {
            out += "FAILED: the scene file does not exist: " + options.scenePath + "\n";
            return CliExitCode::CommandFailed;
        }
        if (!options.scenePath.empty() && !newScene && !app.LoadSceneFile(options.scenePath))
        {
            out += "FAILED: could not load the scene: " + options.scenePath + "\n";
            return CliExitCode::CommandFailed;
        }

        EditorCommandResult result = commands.Execute(options.commandName, app, commandArgs);

        // 成功して、シーンを書き換えるコマンドなら、保存する(失敗したときは、ファイルを変えない)。
        if (result.succeeded && command->modifiesScene)
        {
            if (app.SaveSceneFile(options.scenePath))
            {
                result.details.push_back(std::string(newScene ? "created scene: " : "saved scene: ") + options.scenePath);
            }
            else
            {
                result.succeeded = false;
                result.message += " (but could not save the scene: " + options.scenePath + ")";
            }
        }

        out += std::string(result.succeeded ? "OK: " : "FAILED: ") + result.message + "\n";
        for (const std::string& detail : result.details)
        {
            out += "  " + detail + "\n";
        }

        if (!options.jsonOutPath.empty())
        {
            engine::ArchiveValue root = engine::ArchiveValue::MakeObject();
            root.Set("command", engine::ArchiveValue(options.commandName));
            root.Set("succeeded", engine::ArchiveValue(result.succeeded));
            root.Set("message", engine::ArchiveValue(result.message));
            engine::ArchiveValue& details = root.Set("details", engine::ArchiveValue::MakeArray());
            for (const std::string& detail : result.details)
            {
                details.Append(engine::ArchiveValue(detail));
            }
            if (!result.data.IsNull())
            {
                root.Set("data", result.data); // 機械が読むための結果(actor.list の全件など)
            }
            if (!WriteJsonFile(root, options.jsonOutPath, out))
            {
                return CliExitCode::CommandFailed;
            }
        }
        return result.succeeded ? CliExitCode::Success : CliExitCode::CommandFailed;
    }
}

#endif
