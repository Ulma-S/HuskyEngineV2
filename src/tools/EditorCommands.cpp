// EditorCommands.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCommands.h"
#include "tools/EditorLog.h"

#if HUSKY_DEVELOPMENT

#include "core/TypeRegistry.h"      // actor.spawn: 型の名前から作る
#include "engine/Actor.h"
#include "engine/Collider2D.h"
#include "engine/AssetLoader.h"     // actor.spawn: プレハブを読む
#include "engine/Prefab.h"          // actor.spawn、prefab.create
#include "platform/FileSystem.h"    // prefab.create: ファイルを書く
#include "engine/JsonFormat.h"       // property.set: 値を人に見せる
#include "engine/ObjectSerializer.h" // property.set: 値の確かめと書き込み
#include "engine/SceneSerializer.h"  // actor.destroy / actor.restore: アクター1体を控える・作り直す
#include "engine/AssetDatabase.h"   // CreateMissingMetaFiles、Scan
#include "engine/SpriteSheetData.h" // NormalizeSpriteSheetsInFolder

#include <algorithm> // std::sort

#include <utility> // std::move

namespace husky::tools
{
    EditorCommandResult EditorCommandResult::Success(std::string message)
    {
        EditorCommandResult result;
        result.succeeded = true;
        result.message = std::move(message);
        return result;
    }

    EditorCommandResult EditorCommandResult::Failure(std::string message)
    {
        EditorCommandResult result;
        result.succeeded = false;
        result.message = std::move(message);
        return result;
    }

    const char* GetArgTypeName(EditorArgType type)
    {
        switch (type)
        {
        case EditorArgType::Bool: return "bool";
        case EditorArgType::Int: return "int";
        case EditorArgType::Float: return "float";
        case EditorArgType::String: return "string";
        case EditorArgType::Any: return "any";
        }
        return "";
    }

    bool EditorCommand::HasRequiredArgs() const
    {
        for (const EditorCommandArg& arg : args)
        {
            if (arg.required)
            {
                return true;
            }
        }
        return false;
    }

    std::string ValidateCommandArgs(const EditorCommand& command, const engine::ArchiveValue& args)
    {
        using Type = engine::ArchiveValue::Type;
        if (!args.IsNull() && args.GetType() != Type::Object)
        {
            return "Arguments must be a JSON object, like {\"name\": value}";
        }

        // 1. 渡された名前が、どれも知っている引数か(打ち間違いを、黙って無視しないため)。
        for (std::size_t i = 0; i < args.GetMemberCount(); ++i)
        {
            const std::string& name = args.GetMemberName(i);
            bool known = false;
            for (const EditorCommandArg& arg : command.args)
            {
                known = known || arg.name == name;
            }
            if (!known)
            {
                return "Unknown argument '" + name + "' for " + command.name;
            }
        }

        // 2. 引数ごとに、あるか、種類が合っているか。
        for (const EditorCommandArg& arg : command.args)
        {
            const engine::ArchiveValue* value = args.Find(arg.name);
            const std::string what = "'" + arg.name + "' (" + GetArgTypeName(arg.type) + ")";
            if (value == nullptr)
            {
                if (arg.required)
                {
                    return "Missing argument " + what + ": " + arg.description;
                }
                continue;
            }
            const Type type = value->GetType();
            bool matches = false;
            switch (arg.type)
            {
            case EditorArgType::Bool: matches = type == Type::Bool; break;
            case EditorArgType::Int: matches = type == Type::Int; break;
            // JSON では 100 と 100.0 の区別があいまいなので、数を受け取る引数には整数も受け付ける。
            case EditorArgType::Float: matches = type == Type::Float || type == Type::Int; break;
            case EditorArgType::String: matches = type == Type::String; break;
            // どの種類でもよい(null も「何も指さない」という値として受け付ける。アセットの参照を外すときなど)。
            case EditorArgType::Any: matches = true; break;
            }
            if (!matches)
            {
                return "Argument " + what + " has the wrong type: " + arg.description;
            }
        }
        return std::string();
    }

    bool EditorCommandRegistry::Register(EditorCommand command)
    {
        if (command.name.empty() || !command.execute || Find(command.name) != nullptr)
        {
            return false;
        }
        // std::move: command の中身(文字列や関数)を、コピーせずに一覧へ移す。移した後の command は空になるが、もう使わない。
        m_commands.push_back(std::move(command));
        return true;
    }

    const EditorCommand* EditorCommandRegistry::Find(const std::string& name) const
    {
        for (const EditorCommand& command : m_commands)
        {
            if (command.name == name)
            {
                return &command;
            }
        }
        return nullptr;
    }

    const char* EditorCommandRegistry::GetUnavailableReason(const EditorCommand& command, engine::Application& app)
    {
        return command.whyUnavailable ? command.whyUnavailable(app) : nullptr;
    }

    EditorCommandResult EditorCommandRegistry::Execute(const std::string& name, engine::Application& app,
                                                       const engine::ArchiveValue& args)
    {
        return Run(name, app, args, true, true);
    }

    EditorCommandResult EditorCommandRegistry::Run(const std::string& name, engine::Application& app,
                                                   const engine::ArchiveValue& args, bool record, bool report)
    {
        EditorCommandResult result;
        if (const EditorCommand* command = Find(name))
        {
            const char* reason = GetUnavailableReason(*command, app);
            if (reason != nullptr)
            {
                result = EditorCommandResult::Failure(command->label + " is not available now: " + reason);
            }
            else if (const std::string argError = ValidateCommandArgs(*command, args); !argError.empty())
            {
                // 引数は、実行する前に形で確かめる(コマンドの中で1つずつ確かめなくて済み、理由の書き方もそろう)。
                result = EditorCommandResult::Failure(argError);
            }
            else
            {
                EditorCommandContext context{app, args};
                result = command->execute(context);
            }
        }
        else
        {
            result = EditorCommandResult::Failure("Unknown command: " + name);
        }

        // 元に戻せる変更なら、履歴に積む。新しい変更をしたら、やり直しの履歴は捨てる。
        if (record && result.succeeded && !result.undo.IsEmpty() && !result.redo.IsEmpty())
        {
            if (m_undoHistory.size() >= MaxHistory)
            {
                m_undoHistory.erase(m_undoHistory.begin()); // いちばん古いものを捨てる
            }
            m_undoHistory.push_back(HistoryEntry{result.message, result.undo, result.redo});
            m_redoHistory.clear();
        }

        if (!report)
        {
            return result;
        }
        // 人が後から追えるように、ログにも出す(画面の窓を閉じていても分かる)。
        if (m_logResults)
        {
            if (result.succeeded)
                HUSKY_LOG(EditorLog::CommandSucceeded, name, result.message);
            else
                HUSKY_LOG(EditorLog::CommandFailed, name, result.message);
            for (const std::string& detail : result.details)
            {
                HUSKY_LOG(EditorLog::CommandDetail, detail);
            }
        }
        m_lastCommandName = name;
        m_lastResult = result;
        return result;
    }

    std::size_t EditorCommandRegistry::HandleShortcuts(engine::Application& app)
    {
        // 毎フレーム呼ばれるので、キーが押されていないフレームでは何も確保しない(作業用の vector も作らない。
        // MSVC の Debug の STL は、空の vector を作るだけでも管理用のメモリを確保する)。
        // コマンドの中で一覧に登録が足されると、要素の場所が変わり得るので、範囲 for ではなく番号で回し、
        // 実行する前に名前をコピーしておく(コピーは、押されたときだけ)。
        std::size_t executed = 0;
        const engine::Input& input = app.GetInput();
        const bool ctrlDown = input.IsDown(engine::Key::LeftCtrl) || input.IsDown(engine::Key::RightCtrl);
        for (std::size_t i = 0; i < m_commands.size(); ++i)
        {
            const engine::Key shortcut = m_commands[i].shortcut;
            // Ctrl の状態も合っているときだけ(Ctrl+Z のコマンドは Ctrl を押しているとき、ほかは押していないとき)。
            if (shortcut != engine::Key::Unknown && m_commands[i].shortcutCtrl == ctrlDown && input.WasPressed(shortcut))
            {
                const std::string name = m_commands[i].name;
                Execute(name, app);
                ++executed;
            }
        }
        return executed;
    }

    EditorCommandResult EditorCommandRegistry::Undo(engine::Application& app)
    {
        return Replay(app, m_undoHistory, m_redoHistory, true);
    }

    EditorCommandResult EditorCommandRegistry::Redo(engine::Application& app)
    {
        return Replay(app, m_redoHistory, m_undoHistory, false);
    }

    const char* EditorCommandRegistry::WhyCannotUndo(engine::Application& app) const
    {
        return WhyCannotReplay(app, m_undoHistory, true);
    }

    const char* EditorCommandRegistry::WhyCannotRedo(engine::Application& app) const
    {
        return WhyCannotReplay(app, m_redoHistory, false);
    }

    void EditorCommandRegistry::ClearHistory()
    {
        m_undoHistory.clear();
        m_redoHistory.clear();
    }

    const char* EditorCommandRegistry::WhyCannotReplay(engine::Application& app, const std::vector<HistoryEntry>& history,
                                                       bool isUndo) const
    {
        if (history.empty())
        {
            return isUndo ? "Nothing to undo" : "Nothing to redo";
        }
        // 戻す(やり直す)コマンドが今は使えなければ、その理由をそのまま見せる(再生中は property.set が使えない、など)。
        const EditorCommandCall& call = isUndo ? history.back().undo : history.back().redo;
        const EditorCommand* command = Find(call.name);
        if (command == nullptr)
        {
            return "The command to replay is not registered";
        }
        return GetUnavailableReason(*command, app);
    }

    EditorCommandResult EditorCommandRegistry::Replay(engine::Application& app, std::vector<HistoryEntry>& from,
                                                      std::vector<HistoryEntry>& to, bool isUndo)
    {
        if (const char* reason = WhyCannotReplay(app, from, isUndo))
        {
            return EditorCommandResult::Failure(reason); // 履歴はそのまま(使えるようになれば、後で戻せる)
        }
        // 実行する前に、履歴から取り出しておく(実行の中で履歴が変わっても、取り出した1件は壊れない)。
        // std::move: 文字列や値の木をコピーせずに移す。
        HistoryEntry entry = std::move(from.back());
        from.pop_back();
        const EditorCommandCall& call = isUndo ? entry.undo : entry.redo;
        // 戻すときは、新しい変更として履歴に積まない(record = false)。ログと「最後の結果」は、呼んだ側が出す。
        EditorCommandResult inner = Run(call.name, app, call.args, false, false);

        EditorCommandResult result;
        result.succeeded = inner.succeeded;
        result.message = std::string(isUndo ? "Undo: " : "Redo: ") + entry.label;
        if (inner.succeeded)
        {
            result.details.push_back(inner.message); // 実際にしたこと("Set Player (Actor).speed = 1" など)
            to.push_back(std::move(entry));
        }
        else
        {
            // 戻せなかった(アクターが消えたなど)。この1件は捨てる(残すと、ずっとそこで詰まるため)。
            result.message += " (failed, removed from the history)";
            result.details.push_back(inner.message);
        }
        return result;
    }

    EditorCommand MakeUndoCommand(EditorCommandRegistry& commands)
    {
        EditorCommand command;
        command.name = "editor.undo";
        command.label = "Undo";
        command.description = "Undo the last change made by a command (like property.set).";
        command.group = "history";
        // &commands: 一覧への参照を持たせる(一覧はコマンドより長く生きる)。
        command.shortcut = engine::Key::Z;
        command.shortcutCtrl = true; // Ctrl+Z
        command.execute = [&commands](EditorCommandContext& context) { return commands.Undo(context.app); };
        command.whyUnavailable = [&commands](engine::Application& app) { return commands.WhyCannotUndo(app); };
        return command;
    }

    EditorCommand MakeRedoCommand(EditorCommandRegistry& commands)
    {
        EditorCommand command;
        command.name = "editor.redo";
        command.label = "Redo";
        command.description = "Redo the last change that was undone.";
        command.group = "history";
        command.shortcut = engine::Key::Y;
        command.shortcutCtrl = true; // Ctrl+Y
        command.execute = [&commands](EditorCommandContext& context) { return commands.Redo(context.app); };
        command.whyUnavailable = [&commands](engine::Application& app) { return commands.WhyCannotRedo(app); };
        return command;
    }

    EditorCommand MakeSaveSceneCommand(std::vector<std::string> paths)
    {
        EditorCommand command;
        command.name = "scene.save";
        command.label = "Save scene";
        command.description = "Write the current scene to the scene file (source assets and next to the exe).";
        command.shortcut = engine::Key::F5;
        // paths はラムダ式にコピーではなく移して持たせる(コマンドが生きている間ずっと使う)。
        command.execute = [paths = std::move(paths)](EditorCommandContext& context) {
            if (paths.empty())
            {
                return EditorCommandResult::Failure("No scene file to write");
            }
            std::vector<std::string> failed;
            for (const std::string& path : paths)
            {
                if (!context.app.SaveSceneFile(path))
                {
                    failed.push_back(path);
                }
            }
            const std::size_t actorCount = context.app.GetScene().actors.size();
            EditorCommandResult result;
            if (failed.empty())
            {
                result = EditorCommandResult::Success("Saved scene (" + std::to_string(actorCount) + " actors) to " +
                                                      std::to_string(paths.size()) + " file(s)");
                result.details = paths;
            }
            else
            {
                result = EditorCommandResult::Failure("Could not write " + std::to_string(failed.size()) + " of " +
                                                      std::to_string(paths.size()) + " scene file(s)");
                for (const std::string& path : failed)
                {
                    result.details.push_back("failed: " + path);
                }
            }
            return result;
        };
        return command;
    }

    EditorCommand MakeRefreshAssetsCommand(std::string assetsDirectory)
    {
        EditorCommand command;
        command.name = "asset.refresh";
        command.label = "Refresh assets";
        command.description = "Create missing .meta files, fix sprite sheets (IDs), and rebuild the asset table. "
                              "Run this after adding or editing files in the assets folder.";
        command.execute = [directory = std::move(assetsDirectory)](EditorCommandContext& context) {
            // 1 と 2: ファイルを直す(エディタの役目)。どちらも、直したものが無ければ何も書かない。
            const std::size_t createdMeta = engine::CreateMissingMetaFiles(directory);
            const std::size_t fixedSheets = engine::NormalizeSpriteSheetsInFolder(directory);

            // 3: 直した後のファイルで、表を作り直す。
            engine::AssetScanReport report;
            if (!context.app.GetAssetDatabase().Scan(directory, &report))
            {
                return EditorCommandResult::Failure("Could not read the assets folder: " + directory);
            }

            // 要約は「何個になったか」と「何を直したか」。人が一目で分かるように、直したものが無ければ書かない。
            std::string summary = std::to_string(report.assetCount) + " assets";
            if (createdMeta > 0)
            {
                summary += ", created " + std::to_string(createdMeta) + " .meta";
            }
            if (fixedSheets > 0)
            {
                summary += ", fixed " + std::to_string(fixedSheets) + " sprite sheet(s)";
            }

            EditorCommandResult result = report.HasProblems()
                                             ? EditorCommandResult::Failure(summary + " (problems found, see log)")
                                             : EditorCommandResult::Success(summary);
            result.details.push_back("folder: " + directory);
            result.details.push_back("created .meta: " + std::to_string(createdMeta));
            result.details.push_back("fixed sprite sheets: " + std::to_string(fixedSheets));
            // 問題は、数があるものだけを、何をすればよいかと一緒に並べる。
            if (report.missingMetaCount > 0)
            {
                result.details.push_back("assets without .meta: " + std::to_string(report.missingMetaCount) +
                                         " (could not create them)");
            }
            if (report.invalidMetaCount > 0)
            {
                result.details.push_back("unreadable .meta: " + std::to_string(report.invalidMetaCount) +
                                         " (fix or delete the file)");
            }
            if (report.orphanMetaCount > 0)
            {
                result.details.push_back(".meta without asset: " + std::to_string(report.orphanMetaCount) +
                                         " (move it with its asset, or delete it)");
            }
            if (report.duplicateGuidCount > 0)
            {
                result.details.push_back("duplicate GUIDs: " + std::to_string(report.duplicateGuidCount) +
                                         " (a copied .meta? give one of them a new GUID)");
            }
            return result;
        };
        return command;
    }

    namespace
    {
        // プレハブを選ばせる選択肢: アセットの表にある .prefab すべて(値は GUID、表示はパス。パスの順)。
        std::vector<EditorArgChoice> PrefabChoices(engine::Application& app, const engine::ArchiveValue&)
        {
            std::vector<EditorArgChoice> choices;
            app.GetAssetDatabase().ForEachAsset([&choices](const core::Guid& guid, const std::string& path) {
                if (engine::AssetLoader::CanLoadAs(engine::PrefabAsset::StaticType(), path))
                {
                    choices.push_back({guid.ToString(), path});
                }
            });
            std::sort(choices.begin(), choices.end(),
                      [](const EditorArgChoice& a, const EditorArgChoice& b) { return a.label < b.label; });
            return choices;
        }

        // 引数で書かれたプレハブ(GUID の文字列か、assets の中のパス)を、読み終わったアセットにする。
        // 読み込み中なら、読み終わるまで待つ(エディタの操作なので、少し止まってもよい。置く操作をその場で終わらせるため)。
        // 見つからない・読めないときは nullptr を返し、error に理由を書く。
        const engine::PrefabAsset* LoadPrefabFromArg(engine::Application& app, const std::string& text, std::string& error)
        {
            core::Guid guid;
            if (!core::Guid::Parse(text, guid))
            {
                const core::Guid* found = app.GetAssetDatabase().FindGuid(text);
                if (found == nullptr)
                {
                    error = "No prefab: " + text +
                            " (give its GUID or its path in the assets folder, like \"prefabs/a.prefab\")";
                    return nullptr;
                }
                guid = *found;
            }
            const std::string* path = app.GetAssetDatabase().FindPath(guid);
            if (path == nullptr || !engine::AssetLoader::CanLoadAs(engine::PrefabAsset::StaticType(), *path))
            {
                error = "Not a prefab: " + text +
                        (path != nullptr ? " (" + *path + ")" : std::string(" (not in the asset table)"));
                return nullptr;
            }

            engine::AssetLoader& loader = app.GetAssetLoader();
            const engine::PrefabAsset* prefab = loader.ResolvePrefab(loader.LoadPrefab(guid));
            if (prefab != nullptr && prefab->GetState() == engine::AssetState::Loading)
            {
                loader.WaitForAllLoads();
            }
            if (prefab == nullptr || !prefab->IsLoaded())
            {
                error = "Could not load the prefab: " + *path + " (see log)";
                return nullptr;
            }
            return prefab;
        }
    }

    namespace
    {
        // actor.spawn の Undo / Redo に使う(中身は actor.destroy の近くに書いている)。
        EditorCommandCall MakeDestroyCall(const core::Guid& guid);
        EditorCommandCall MakeRestoreCall(const engine::ArchiveValue& data, const std::string& parentGuid);
    }

    EditorCommand MakeSpawnActorCommand()
    {
        EditorCommand command;
        command.name = "actor.spawn";
        command.label = "Spawn actor";
        command.description = "Place a new actor in the scene: either from a prefab (with its components and values), "
                              "or a bare actor of the given type (no components). Give one of prefab or type.";
        command.modifiesScene = true;
        EditorCommandArg prefabArg{"prefab", EditorArgType::String,
                                   "Prefab to place: its GUID, or its path in the assets folder like \"prefabs/a.prefab\"",
                                   false};
        prefabArg.choices = PrefabChoices;
        EditorCommandArg typeArg{"type", EditorArgType::String,
                                 "Type name of a bare actor, like \"BouncerActor\" (must derive from Actor). "
                                 "Use this when there is no prefab",
                                 false};
        // 選べるのは、型の登録簿にある Actor の子孫(エンジンの型と、ゲームが登録した型)。
        typeArg.choices = [](engine::Application& app, const engine::ArchiveValue&) {
            std::vector<EditorArgChoice> names;
            const core::TypeRegistry& types = app.GetTypeRegistry();
            for (std::size_t i = 0; i < types.GetCount(); ++i)
            {
                const core::TypeInfo* type = types.GetType(i);
                if (type != nullptr && type->IsA(engine::Actor::StaticType()))
                {
                    names.push_back({type->GetName(), std::string()});
                }
            }
            return names;
        };
        command.args = {
            prefabArg,
            typeArg,
            {"x", EditorArgType::Float, "X position in world units", false, engine::ArchiveValue(0.0)},
            {"y", EditorArgType::Float, "Y position in world units", false, engine::ArchiveValue(0.0)},
        };
        command.execute = [](EditorCommandContext& context) {
            // 引数は Execute が形で確かめ済みなので、ここでは種類の心配をせずに読める。
            const engine::ArchiveValue* prefabValue = context.args.Find("prefab");
            const engine::ArchiveValue* typeValue = context.args.Find("type");
            const engine::ArchiveValue* xValue = context.args.Find("x");
            const engine::ArchiveValue* yValue = context.args.Find("y");
            const float x = xValue != nullptr ? static_cast<float>(xValue->AsFloat()) : 0.0f;
            const float y = yValue != nullptr ? static_cast<float>(yValue->AsFloat()) : 0.0f;
            if ((prefabValue == nullptr) == (typeValue == nullptr)) // 両方ある、または両方無い
            {
                return EditorCommandResult::Failure("Give exactly one of prefab or type");
            }

            engine::Scene& scene = context.app.GetScene();
            const core::TypeRegistry& types = context.app.GetTypeRegistry();
            engine::Actor* actor = nullptr;
            std::vector<std::string> warnings;
            std::string what; // 結果の1行に書く、置いたものの名前
            if (prefabValue != nullptr)
            {
                // プレハブから置く: アクター、コンポーネント、値がプレハブのとおりに作られ、元のプレハブを覚える。
                std::string error;
                const engine::PrefabAsset* prefab = LoadPrefabFromArg(context.app, prefabValue->AsString(), error);
                if (prefab == nullptr)
                {
                    return EditorCommandResult::Failure(error);
                }
                engine::Prefab::InstantiateResult placed = engine::Prefab::Instantiate(*prefab, scene, types);
                warnings = std::move(placed.warnings);
                actor = placed.actor;
                what = *context.app.GetAssetDatabase().FindPath(prefab->GetGuid());
                if (actor == nullptr)
                {
                    EditorCommandResult failed = EditorCommandResult::Failure("Could not place the prefab " + what);
                    failed.details = std::move(warnings);
                    return failed;
                }
            }
            else
            {
                // 型だけで置く: 型の名前から型を探し、アクターの子孫かを確かめてから作る(シーンのファイルを読むときと同じ道具を使う)。
                const std::string& typeName = typeValue->AsString();
                const core::TypeInfo* type = types.Find(typeName);
                if (type == nullptr)
                {
                    return EditorCommandResult::Failure("Unknown type: " + typeName);
                }
                if (!type->IsA(engine::Actor::StaticType()))
                {
                    return EditorCommandResult::Failure(typeName + " is not an actor");
                }
                actor = core::Cast<engine::Actor>(types.Create(typeName, context.app.GetRegistry()));
                if (actor == nullptr || !scene.AddLoadedActor(*actor))
                {
                    return EditorCommandResult::Failure("Could not create " + typeName + " (out of object memory?)");
                }
                what = typeName;
            }
            actor->transform.position.x = x;
            actor->transform.position.y = y;
            // 位置は %g で短く書く(std::to_string は 100.000000 のように桁を並べて読みにくい)。
            char position[64];
            std::snprintf(position, sizeof(position), " at (%g, %g)", static_cast<double>(x), static_cast<double>(y));
            EditorCommandResult result = EditorCommandResult::Success("Spawned " + what + position);
            result.details = std::move(warnings);
            // 機械向け: 置いたアクターの guid(続けて property.set などで指すため)。
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("guid", engine::ArchiveValue(actor->guid.ToString()));
            // Undo は guid で消す。Redo は「置き直す」ではなく、置いた直後のアクターの控えから同じ guid で戻す
            // (置き直すと新しい guid になり、この後の履歴(property.set など)が指す guid と食い違うため)。
            result.undo = MakeDestroyCall(actor->guid);
            // 控えには子孫も入れる(子を含むプレハブ。HIER-2b。子も同じ guid で戻り、この後の履歴とつながる)
            std::vector<engine::Actor*> placedActors = {actor};
            actor->GetDescendants(placedActors);
            result.redo = MakeRestoreCall(engine::SceneSerializer::WriteActors(scene, placedActors).value, std::string()); // 置いた直後は親なし
            return result;
        };
        return command;
    }

    namespace
    {
        // 位置を短く書く("(12, 34.5)")。
        std::string FormatPosition(float x, float y)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "(%g, %g)", static_cast<double>(x), static_cast<double>(y));
            return text;
        }

        // アクターを選ばせる選択肢: シーンの生きているアクターすべて(値は guid、表示は名前や型)。
        std::vector<EditorArgChoice> ActorChoices(engine::Application& app, const engine::ArchiveValue&)
        {
            std::vector<EditorArgChoice> choices;
            app.GetScene().ForEachActor([&choices](engine::Actor& actor) {
                choices.push_back({actor.guid.ToString(), DescribeActor(actor)});
            });
            return choices;
        }

        // property.set で書き換えられるプロパティか(アクター自身の、値1つで表せるもの。guid は変えさせない)。
        bool IsSettableProperty(const core::Property& property)
        {
            switch (property.GetType())
            {
            case core::PropertyType::Bool:
            case core::PropertyType::Int32:
            case core::PropertyType::Float:
            case core::PropertyType::Int64:
            case core::PropertyType::Double:
            case core::PropertyType::Enum: // 値の名前の文字列(数でも、登録した値なら)
            case core::PropertyType::String:
            case core::PropertyType::Struct: // 構造体は {"x": 1} のような形で、書いた値だけを書き換える
            case core::PropertyType::AssetRef: // GUID の文字列か、null(何も指さない)
                return true;
            case core::PropertyType::Array:
                // 配列(SF-2b): JSON の配列で、まるごと入れ替える。参照の配列は、要素ごとに参照の形で(REF-1)。
                return !property.IsReference() || IsReferenceArray(property);
            case core::PropertyType::ObjectHandle:
            case core::PropertyType::ObjectRef:
                // 参照(REF-1): アクターの guid(か名前)、コンポーネントは {"actor", "component"}、null で外す
                return true;
            default:
                return false; // ハンドル、配列、guid は、今は書き換えない
            }
        }

        // アクターのコンポーネントのうち、型の名前が typeName の型(か子孫)の最初のもの。
        // 見つからなければ nullptr を返し、error に人が読める理由を書く(付いているコンポーネントを並べる)。
        engine::Component* FindComponentFromArg(engine::Application& app, const engine::Actor& actor,
                                                const std::string& typeName, std::string& error)
        {
            const core::TypeInfo* type = app.GetTypeRegistry().Find(typeName);
            std::string attached; // 付いているコンポーネントの型の名前(断るときに見せる)
            for (const core::ObjectHandle handle : actor.components)
            {
                engine::Component* component = app.GetRegistry().Resolve<engine::Component>(handle);
                if (component == nullptr || component->IsPendingDestroy())
                {
                    continue;
                }
                if (type != nullptr && component->GetType().IsA(*type))
                {
                    return component;
                }
                attached += attached.empty() ? "" : ", ";
                attached += component->GetType().GetName();
            }
            error = DescribeActor(actor) + " has no component '" + typeName + "' (it has: " +
                    (attached.empty() ? std::string("none") : attached) + ")";
            return nullptr;
        }

        // property.set の引数(actor、component)から、書き換える相手(アクターか、そのコンポーネント)を探す。
        // 見つからなければ nullptr と理由。actorOut には、見つかったアクターを入れる。
        core::Object* FindPropertyOwner(engine::Application& app, const engine::ArchiveValue& args,
                                        engine::Actor*& actorOut, std::string& error)
        {
            const engine::ArchiveValue* actorValue = args.Find("actor");
            actorOut = actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), error) : nullptr;
            if (actorOut == nullptr)
            {
                return nullptr;
            }
            const engine::ArchiveValue* componentValue = args.Find("component");
            if (componentValue == nullptr)
            {
                return actorOut;
            }
            return FindComponentFromArg(app, *actorOut, componentValue->AsString(), error);
        }

        // 「transform.position.x」のような、点でつないだ道筋を区切る。
        std::vector<std::string> SplitPropertyPath(const std::string& path)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t dot = path.find('.', start);
                parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
                if (dot == std::string::npos)
                {
                    return parts;
                }
                start = dot + 1;
            }
        }

        // 道筋をたどって、最後のプロパティを探す。途中は構造体でなければならない。見つからなければ nullptr。
        //   例: Actor の "transform.position.x" → Transform の position(glm::vec3)→ その x(float)
        const core::Property* FindPropertyByPath(const core::TypeInfo& type, const std::vector<std::string>& parts)
        {
            const core::TypeInfo* current = &type;
            const core::Property* property = nullptr;
            for (const std::string& part : parts)
            {
                if (current == nullptr) // 前の段が構造体ではなかった(中に入れない)
                {
                    return nullptr;
                }
                property = current->FindProperty(part.c_str());
                if (property == nullptr)
                {
                    return nullptr;
                }
                current = property->GetStructType(); // 構造体なら、次の段はその中を探す
            }
            return property;
        }

        // property.set で書き換えられるプロパティの道筋を、構造体の中まで入って全部集める(選択肢の一覧に使う)。
        void CollectSettablePaths(const core::TypeInfo& type, const std::string& prefix, std::vector<EditorArgChoice>& out)
        {
            type.ForEachProperty([&](const core::Property& property) {
                const std::string path = prefix + property.GetName();
                if (const core::TypeInfo* structType = property.GetStructType())
                {
                    CollectSettablePaths(*structType, path + ".", out);
                }
                else if (IsSettableProperty(property))
                {
                    out.push_back({path, std::string()});
                }
            });
        }
    }

    std::string DescribeActor(const engine::Actor& actor)
    {
        const char* typeName = actor.GetType().GetName();
        if (actor.name.empty())
        {
            return std::string(typeName) + " at " + FormatPosition(actor.transform.position.x, actor.transform.position.y);
        }
        return actor.name + " (" + typeName + ")";
    }

    engine::ArchiveValue MakeReferenceArg(engine::Application& app, core::ObjectHandle handle)
    {
        const core::Object* object = app.GetRegistry().Resolve(handle);
        if (object == nullptr || object->IsPendingDestroy())
        {
            return engine::ArchiveValue(); // 何も指さない・もう無い
        }
        if (const engine::Actor* actor = core::Cast<engine::Actor>(object))
        {
            // シーンにいるアクターだけ(guid で引き直せるもの)
            return app.GetScene().FindActorByGuid(actor->guid) == actor ? engine::ArchiveValue(actor->guid.ToString())
                                                                         : engine::ArchiveValue();
        }
        const engine::Component* component = core::Cast<engine::Component>(object);
        const engine::Actor* owner = component != nullptr ? component->GetOwner() : nullptr;
        if (owner == nullptr || app.GetScene().FindActorByGuid(owner->guid) != owner)
        {
            return engine::ArchiveValue(); // シーンのアクターのものではない
        }
        // 同じ型のコンポーネントの中で何個目か(ふつうは 0 で、そのときは書かない)
        std::int64_t index = 0;
        for (const core::ObjectHandle other : owner->components)
        {
            if (other == handle)
            {
                break;
            }
            const core::Object* earlier = app.GetRegistry().Resolve(other);
            if (earlier != nullptr && !earlier->IsPendingDestroy() && &earlier->GetType() == &component->GetType())
            {
                ++index;
            }
        }
        engine::ArchiveValue value = engine::ArchiveValue::MakeObject();
        value.Set("actor", engine::ArchiveValue(owner->guid.ToString()));
        value.Set("component", engine::ArchiveValue(component->GetType().GetName()));
        if (index > 0)
        {
            value.Set("index", engine::ArchiveValue(index));
        }
        return value;
    }

    bool FindReferenceFromArg(engine::Application& app, const engine::ArchiveValue& value, core::ObjectHandle& out,
                              std::string& error)
    {
        out = {};
        if (value.IsNull())
        {
            return true; // どこも指さない
        }
        if (value.GetType() == engine::ArchiveValue::Type::String)
        {
            engine::Actor* actor = FindActorFromArg(app.GetScene(), value.AsString(), error);
            if (actor == nullptr)
            {
                return false;
            }
            out = actor->GetHandle();
            return true;
        }
        const engine::ArchiveValue* actorValue = value.Find("actor");
        const engine::ArchiveValue* componentValue = value.Find("component");
        if (value.GetType() != engine::ArchiveValue::Type::Object || actorValue == nullptr || componentValue == nullptr)
        {
            error = "A reference must be null, an actor (name or guid), or {\"actor\": ..., \"component\": type}";
            return false;
        }
        engine::Actor* actor = FindActorFromArg(app.GetScene(), actorValue->AsString(), error);
        if (actor == nullptr)
        {
            return false;
        }
        const engine::ArchiveValue* indexValue = value.Find("index");
        std::int64_t remaining = indexValue != nullptr ? indexValue->AsInt() : 0;
        for (const core::ObjectHandle handle : actor->components)
        {
            const core::Object* component = app.GetRegistry().Resolve(handle);
            if (component == nullptr || component->IsPendingDestroy() ||
                componentValue->AsString() != component->GetType().GetName())
            {
                continue;
            }
            if (remaining-- == 0)
            {
                out = handle;
                return true;
            }
        }
        error = DescribeActor(*actor) + " has no component '" + componentValue->AsString() + "'" +
                (indexValue != nullptr ? " #" + std::to_string(indexValue->AsInt()) : std::string());
        return false;
    }

    bool IsReferenceArray(const core::Property& property)
    {
        return property.GetType() == core::PropertyType::Array && property.GetArrayInfo() != nullptr &&
               property.GetArrayInfo()->element.GetType() == core::PropertyType::ObjectRef;
    }

    engine::ArchiveValue MakeReferenceArrayArg(engine::Application& app, const core::Property& property,
                                               const void* container)
    {
        engine::ArchiveValue values = engine::ArchiveValue::MakeArray();
        if (!IsReferenceArray(property))
        {
            return values;
        }
        const core::ArrayInfo& array = *property.GetArrayInfo();
        const void* address = property.GetArrayAddressAt(container);
        const std::size_t count = array.size(address);
        for (std::size_t i = 0; i < count; ++i)
        {
            const core::ObjectReference* element = array.element.GetValuePtrAt<core::ObjectReference>(array.atConst(address, i));
            values.Append(MakeReferenceArg(app, element->handle));
        }
        return values;
    }

    bool AssignReferenceArray(engine::Application& app, const core::Property& property, void* container,
                              const engine::ArchiveValue& value, std::string& error)
    {
        if (!IsReferenceArray(property))
        {
            error = std::string("'") + property.GetName() + "' is not an array of references";
            return false;
        }
        if (value.GetType() != engine::ArchiveValue::Type::Array)
        {
            error = "An array of references is needed (each one an actor, a component, or null)";
            return false;
        }
        // 先に全部の相手を引いて確かめる(途中で失敗しても、配列を半分だけ変えないため)
        const core::ArrayInfo& array = *property.GetArrayInfo();
        const core::TypeInfo* allowed = array.element.GetObjectRefType();
        std::vector<core::ObjectHandle> handles(value.GetArraySize());
        for (std::size_t i = 0; i < value.GetArraySize(); ++i)
        {
            if (!FindReferenceFromArg(app, value.At(i), handles[i], error))
            {
                error = "[" + std::to_string(i) + "] " + error;
                return false;
            }
            const core::Object* target = app.GetRegistry().Resolve(handles[i]);
            if (target != nullptr && allowed != nullptr && !target->GetType().IsA(*allowed))
            {
                error = "[" + std::to_string(i) + "] " + target->GetType().GetName() + " is not a " + allowed->GetName();
                return false;
            }
        }
        void* address = property.GetArrayAddressAt(container);
        array.resize(address, 0);
        array.resize(address, handles.size());
        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            array.element.GetValuePtrAt<core::ObjectReference>(array.at(address, i))->handle = handles[i];
        }
        return true;
    }

    engine::Actor* FindActorFromArg(engine::Scene& scene, const std::string& actorText, std::string& error)
    {
        // 1. guid の形なら guid で探す(名前に guid の形の文字列を付けるアクターは無いものとする)。
        core::Guid guid;
        if (core::Guid::Parse(actorText, guid))
        {
            engine::Actor* actor = scene.FindActorByGuid(guid);
            if (actor == nullptr)
            {
                error = "No actor has the guid " + actorText + " (see actor.list)";
            }
            return actor;
        }

        // 2. 名前で探す。重なっていたら、黙ってどれかを選ばず、候補を並べて断る。
        std::vector<engine::Actor*> matches;
        scene.ForEachActor([&](engine::Actor& actor) {
            if (actor.name == actorText)
            {
                matches.push_back(&actor);
            }
        });
        if (matches.size() == 1)
        {
            return matches[0];
        }
        if (matches.empty())
        {
            error = "No actor named '" + actorText + "' (see actor.list)";
            return nullptr;
        }
        error = "'" + actorText + "' matches " + std::to_string(matches.size()) + " actors; use the guid of one of them:";
        constexpr std::size_t MaxListed = 5;
        for (std::size_t i = 0; i < matches.size() && i < MaxListed; ++i)
        {
            const engine::Actor& actor = *matches[i];
            error += "\n  " + actor.guid.ToString() + "  " + actor.GetType().GetName() + " at " +
                     FormatPosition(actor.transform.position.x, actor.transform.position.y);
        }
        if (matches.size() > MaxListed)
        {
            error += "\n  ... and " + std::to_string(matches.size() - MaxListed) + " more";
        }
        return nullptr;
    }

    EditorCommand MakeListActorsCommand()
    {
        EditorCommand command;
        command.name = "actor.list";
        command.label = "List actors";
        command.description = "List the actors in the scene with their name, type, guid and position. "
                              "Use the guid to point at an actor whose name is not unique.";
        command.readsScene = true;
        command.args = {
            {"name", EditorArgType::String, "Only actors with this name", false},
            {"type", EditorArgType::String, "Only actors of this type (or a type derived from it)", false},
        };
        command.execute = [](EditorCommandContext& context) {
            const engine::ArchiveValue* nameValue = context.args.Find("name");
            const engine::ArchiveValue* typeValue = context.args.Find("type");
            const core::TypeInfo* filterType = nullptr;
            if (typeValue != nullptr)
            {
                filterType = context.app.GetTypeRegistry().Find(typeValue->AsString());
                if (filterType == nullptr)
                {
                    return EditorCommandResult::Failure("Unknown type: " + typeValue->AsString());
                }
            }

            EditorCommandResult result;
            result.data = engine::ArchiveValue::MakeArray();
            std::size_t count = 0;
            constexpr std::size_t MaxDetails = 20; // 人向けの一覧は読める数で切る(全件は data に入れる)
            context.app.GetScene().ForEachActor([&](engine::Actor& actor) {
                if ((nameValue != nullptr && actor.name != nameValue->AsString()) ||
                    (filterType != nullptr && !actor.GetType().IsA(*filterType)))
                {
                    return;
                }
                ++count;
                if (result.details.size() < MaxDetails)
                {
                    result.details.push_back(actor.guid.ToString() + "  " + DescribeActor(actor));
                }
                engine::ArchiveValue& item = result.data.Append(engine::ArchiveValue::MakeObject());
                item.Set("guid", engine::ArchiveValue(actor.guid.ToString()));
                item.Set("name", engine::ArchiveValue(actor.name));
                item.Set("type", engine::ArchiveValue(actor.GetType().GetName()));
                item.Set("x", engine::ArchiveValue(actor.transform.position.x));
                item.Set("y", engine::ArchiveValue(actor.transform.position.y));
            });
            if (count > MaxDetails)
            {
                result.details.push_back("... and " + std::to_string(count - MaxDetails) +
                                         " more (all of them are in the JSON result)");
            }
            result.succeeded = true;
            result.message = std::to_string(count) + " actor(s)";
            return result;
        };
        return command;
    }

    namespace
    {
        // property.set の1件分(引数 actor、component(省略可)、property、value)を書き換える。property.set と property.setMany の中身。
        // 成功なら true と、actorOut(書き換えたアクター)、old(前の値)、who(人に見せる相手の説明。書き換える前のもの)。
        // 失敗なら false と error(構造体の一部だけが書けたときも、前の値に戻してある)。
        // 値1つを、ファイルと同じ入れ子の形にする。構造体の読み込みは書いてある値だけを書き換えるので、
        //   "transform.position.x" → {"properties": {"transform": {"position": {"x": 値}}}}(ほかの値はそのまま)
        // property.set と data.set が、この形を ObjectSerializer::ReadProperties に渡して1つだけ書き換える。
        engine::ArchiveValue MakeNestedEntry(const std::vector<std::string>& path, const engine::ArchiveValue& entryValue)
        {
            engine::ArchiveValue entry = engine::ArchiveValue::MakeObject();
            engine::ArchiveValue* parent = &entry.Set("properties", engine::ArchiveValue::MakeObject());
            for (std::size_t i = 0; i + 1 < path.size(); ++i)
            {
                parent = &parent->Set(path[i], engine::ArchiveValue::MakeObject());
            }
            parent->Set(path.back(), entryValue);
            return entry;
        }

        // 参照のプロパティ(ObjectHandle / ObjectRef。REF-1)を、引数の値(MakeReferenceArg の形)で書き換える。
        // 書き込みは、読み込みと同じ ObjectSerializer::ReadProperties を通す(番号 0 → 相手、の対応表を渡す)ので、
        // ObjectRef<T> の型の確かめもファイルを読むときとそろう。old には前の値(MakeReferenceArg の形)を入れる。
        bool SetReferenceProperty(engine::Application& app, core::Object& owner, const std::vector<std::string>& path,
                                  const core::Property& property, const engine::ArchiveValue& value,
                                  engine::ArchiveValue& old, std::string& error)
        {
            // 道筋の最後のプロパティの持ち主(構造体の中なら、その構造体)の先頭
            void* container = &owner;
            const core::TypeInfo* type = &owner.GetType();
            for (std::size_t i = 0; i + 1 < path.size(); ++i)
            {
                const core::Property* step = type->FindProperty(path[i].c_str());
                container = step->GetStructAddressAt(container);
                type = step->GetStructType();
            }
            if (IsReferenceArray(property))
            {
                // 参照の配列: 要素ごとに参照の形(guid など)の配列で、まるごと入れ替える
                old = MakeReferenceArrayArg(app, property, container);
                return AssignReferenceArray(app, property, container, value, error);
            }
            core::ObjectHandle& slot = property.GetType() == core::PropertyType::ObjectHandle
                                           ? *property.GetValuePtrAt<core::ObjectHandle>(container)
                                           : property.GetValuePtrAt<core::ObjectReference>(container)->handle;
            const core::ObjectHandle previous = slot;
            old = MakeReferenceArg(app, previous);

            core::ObjectHandle target;
            if (!FindReferenceFromArg(app, value, target, error))
            {
                return false;
            }
            const engine::ObjectSerializer::ReadReport report = engine::ObjectSerializer::ReadProperties(
                MakeNestedEntry(path, target.IsNull() ? engine::ArchiveValue() : engine::ArchiveValue(std::int64_t{0})), owner,
                [target](std::int64_t id) { return id == 0 ? target : core::ObjectHandle{}; });
            if (!report.warnings.empty())
            {
                slot = previous; // 型が違うなど。前の相手に戻す
                error = "Could not set " + path.back() + ": " + report.warnings[0];
                return false;
            }
            return true;
        }

        bool SetOneProperty(engine::Application& app, const engine::ArchiveValue& item, engine::Actor*& actorOut,
                            engine::ArchiveValue& old, std::string& who, std::string& error)
        {
            core::Object* owner = FindPropertyOwner(app, item, actorOut, error);
            if (owner == nullptr)
            {
                return false;
            }
            const engine::ArchiveValue* componentValue = item.Find("component");
            who = DescribeActor(*actorOut) + (componentValue != nullptr ? "." + std::string(owner->GetType().GetName()) : "");

            const engine::ArchiveValue* propertyValue = item.Find("property");
            const engine::ArchiveValue* value = item.Find("value");
            if (propertyValue == nullptr || value == nullptr)
            {
                error = "\"property\" and \"value\" are needed";
                return false;
            }
            const std::string& propertyName = propertyValue->AsString();
            const std::vector<std::string> path = SplitPropertyPath(propertyName);
            const core::Property* property = FindPropertyByPath(owner->GetType(), path);
            if (property == nullptr)
            {
                error = who + " has no property '" + propertyName + "'";
                return false;
            }
            if (!IsSettableProperty(*property))
            {
                error = "'" + propertyName + "' cannot be changed with property.set";
                return false;
            }
            // アクターの親は actor.setParent を通す(輪・段数・Rigidbody2D の決まりを守り、見える場所を保つため。HIER-3)
            if (componentValue == nullptr && path.size() == 1 && path[0] == "parent")
            {
                error = "Use actor.setParent to change the parent";
                return false;
            }

            if (property->GetType() == core::PropertyType::ObjectHandle || property->GetType() == core::PropertyType::ObjectRef ||
                IsReferenceArray(*property))
            {
                return SetReferenceProperty(app, *owner, path, *property, *value, old, error);
            }

            // 前の値(Undo と、人に見せるため)。保存と同じ道具で、相手のプロパティを値の木にして、道筋のとおりに取り出す。
            const engine::ArchiveValue before = engine::ObjectSerializer::Write(*owner);
            const engine::ArchiveValue* found = before.Find("properties");
            for (std::size_t i = 0; i < path.size() && found != nullptr; ++i)
            {
                found = found->Find(path[i]);
            }
            if (found == nullptr)
            {
                error = "Could not read the current value of " + propertyName;
                return false;
            }
            old = *found;

            // 値1つを、ファイルと同じ入れ子の形にする(MakeNestedEntry)。
            auto makeEntry = [&path](const engine::ArchiveValue& entryValue) { return MakeNestedEntry(path, entryValue); };

            // 新しい値を書く: シーンの読み込みと同じ道具(ObjectSerializer::ReadProperties)を通すので、
            // 種類の確かめ(「数ではありません」など)も、ファイルを読むときとそろう。
            const engine::ObjectSerializer::ReadReport report =
                engine::ObjectSerializer::ReadProperties(makeEntry(*value), *owner);
            if (!report.warnings.empty())
            {
                // 構造体では、一部のメンバーだけが書けていることがあるので、前の値を書き戻してそろえる。
                engine::ObjectSerializer::ReadProperties(makeEntry(old), *owner);
                error = "Could not set " + propertyName + ": " + report.warnings[0];
                return false;
            }
            return true;
        }

        // property.set の引数の形の、書き換え1件(Undo / Redo の呼び出しと、property.setMany の values の要素)。
        // actorGuid が空なら actor を書かない(property.setMany の要素)。component が空なら、アクター自身のプロパティ。
        engine::ArchiveValue MakeSetItem(const std::string& actorGuid, const std::string& component, const std::string& property,
                                         const engine::ArchiveValue& value)
        {
            engine::ArchiveValue item = engine::ArchiveValue::MakeObject();
            if (!actorGuid.empty())
            {
                item.Set("actor", engine::ArchiveValue(actorGuid));
            }
            if (!component.empty())
            {
                item.Set("component", engine::ArchiveValue(component));
            }
            item.Set("property", engine::ArchiveValue(property));
            item.Set("value", value);
            return item;
        }

        // 1体のアクターの値を、まとめて書き換える(values は MakeSetItem の形の配列)。property.setMany と prefab.revert の中身。
        // 途中で失敗したら、それまでに書き換えたものを逆の順に戻し、false と error を返す(全部か、何もしないか)。
        // 成功なら olds に、同じ形の「前の値」の配列を、逆の順で入れる(そのまま Undo に使える)。
        bool SetManyProperties(engine::Application& app, engine::Actor& actor, const engine::ArchiveValue& values,
                               engine::ArchiveValue& olds, std::string& error)
        {
            const std::string guid = actor.guid.ToString();
            std::vector<engine::ArchiveValue> undone; // 書き換えた順の「前の値」
            for (std::size_t i = 0; i < values.GetArraySize(); ++i)
            {
                engine::ArchiveValue item = values.At(i);
                // 値ごとに "actor" があれば、そのアクター(子孫の値をまとめて戻すとき。HIER)。無ければ actor
                if (item.GetType() == engine::ArchiveValue::Type::Object && item.Find("actor") == nullptr)
                {
                    item.Set("actor", engine::ArchiveValue(guid));
                }
                engine::Actor* found = nullptr;
                engine::ArchiveValue old;
                std::string who;
                if (item.GetType() != engine::ArchiveValue::Type::Object ||
                    !SetOneProperty(app, item, found, old, who, error))
                {
                    if (error.empty())
                    {
                        error = "each value must be {\"component\", \"property\", \"value\"}";
                    }
                    error = "value #" + std::to_string(i) + ": " + error;
                    for (auto it = undone.rbegin(); it != undone.rend(); ++it) // 逆の順に戻す
                    {
                        const engine::ArchiveValue& back = *it; // 前の値(アクターの guid 入り)
                        std::string ignored;
                        SetOneProperty(app, back, found, old, who, ignored);
                    }
                    return false;
                }
                const engine::ArchiveValue* component = values.At(i).Find("component");
                // 前の値には、どのアクターかを書いておく(子孫の値が混ざっても、Undo で同じアクターに戻せる)
                undone.push_back(MakeSetItem(item.Find("actor")->AsString(),
                                             component != nullptr ? component->AsString() : std::string(),
                                             item.Find("property")->AsString(), old));
            }
            olds = engine::ArchiveValue::MakeArray();
            for (auto it = undone.rbegin(); it != undone.rend(); ++it)
            {
                olds.Append(*it);
            }
            return true;
        }

        // property.setMany の呼び出し(Undo / Redo 用)。アクターは guid で指す。
        EditorCommandCall MakeSetManyCall(const engine::Actor& actor, const engine::ArchiveValue& values)
        {
            EditorCommandCall call;
            call.name = "property.setMany";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("actor", engine::ArchiveValue(actor.guid.ToString()));
            call.args.Set("values", values);
            return call;
        }
    }

    EditorCommand MakeSetPropertyCommand()
    {
        EditorCommand command;
        command.name = "property.set";
        command.label = "Set property";
        command.description = "Change one property of an actor or of one of its components, like its position or speed. "
                              "The actor can be given by name (if unique) or by guid. Can be undone (editor.undo).";
        command.modifiesScene = true;

        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        // 選べるコンポーネントは、選んだアクターに付いているもの。
        EditorCommandArg componentArg{"component", EditorArgType::String,
                                      "Type name of a component on the actor (leave out to change the actor itself)", false};
        componentArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
            std::vector<EditorArgChoice> choices;
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string ignored;
            const engine::Actor* actor =
                actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), ignored) : nullptr;
            if (actor != nullptr)
            {
                for (const core::ObjectHandle handle : actor->components)
                {
                    const engine::Component* component = app.GetRegistry().Resolve<engine::Component>(handle);
                    if (component != nullptr && !component->IsPendingDestroy())
                    {
                        choices.push_back({component->GetType().GetName(), std::string()});
                    }
                }
            }
            return choices;
        };
        // 選べるプロパティは、選んだアクター(か、選んだコンポーネント)の型で決まる(ほかの引数を見て一覧を作る)。
        EditorCommandArg propertyArg{"property", EditorArgType::String,
                                     "Name of the property. Use dots to go inside, like \"transform.position.x\"", true};
        propertyArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
            std::vector<EditorArgChoice> choices;
            engine::Actor* actor = nullptr;
            std::string ignored;
            if (const core::Object* owner = FindPropertyOwner(app, args, actor, ignored))
            {
                CollectSettablePaths(owner->GetType(), std::string(), choices);
            }
            return choices;
        };
        command.args = {
            actorArg,
            componentArg,
            propertyArg,
            {"value", EditorArgType::Any,
             "New value (a number, true/false, a string, {\"name\": value} for a struct, or a guid string / null for an asset), matching the property", true},
        };

        command.execute = [](EditorCommandContext& context) {
            engine::Actor* actor = nullptr;
            engine::ArchiveValue old;
            std::string who;
            std::string error;
            if (!SetOneProperty(context.app, context.args, actor, old, who, error))
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string& propertyName = context.args.Find("property")->AsString();
            const engine::ArchiveValue& value = *context.args.Find("value");
            std::string oldText;
            std::string newText;
            engine::JsonFormat::Write(old, oldText, false);
            engine::JsonFormat::Write(value, newText, false);
            EditorCommandResult result = EditorCommandResult::Success("Set " + who + "." + propertyName + " = " + newText);
            result.details.push_back("was " + oldText);
            result.details.push_back("guid: " + actor->guid.ToString());

            // 戻す呼び出しと、やり直す呼び出し: 同じ property.set を、前の値・新しい値で呼ぶ。
            // アクターは guid で指す(名前を変えても、名前が重なっても、Play / Stop で作り直されても、同じものを指す)。
            const engine::ArchiveValue* componentValue = context.args.Find("component");
            auto makeCall = [&](const engine::ArchiveValue& callValue) {
                EditorCommandCall call;
                call.name = "property.set";
                call.args = MakeSetItem(actor->guid.ToString(), componentValue != nullptr ? componentValue->AsString() : std::string(),
                                        propertyName, callValue);
                return call;
            };
            result.undo = makeCall(old);
            result.redo = makeCall(value);
            return result;
        };
        return command;
    }

    EditorCommand MakeSetManyPropertiesCommand()
    {
        EditorCommand command;
        command.name = "property.setMany";
        command.label = "Set properties";
        command.description = "Change several properties of one actor (and its components) at once, as one step. "
                              "If one of them fails, nothing is changed. Can be undone (editor.undo).";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        command.args = {
            actorArg,
            {"values", EditorArgType::Any,
             "Array of {\"component\": type name (leave out for the actor), \"property\": path, \"value\": new value}, "
             "the same as the arguments of property.set. An item may also have \"actor\" to change another actor "
             "(used to revert child actors together)", true},
        };
        command.execute = [](EditorCommandContext& context) {
            std::string error;
            engine::Actor* actor = FindActorFromArg(context.app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const engine::ArchiveValue& values = *context.args.Find("values");
            if (values.GetType() != engine::ArchiveValue::Type::Array)
            {
                return EditorCommandResult::Failure("\"values\" must be an array");
            }
            const std::string who = DescribeActor(*actor);
            engine::ArchiveValue olds;
            if (!SetManyProperties(context.app, *actor, values, olds, error))
            {
                return EditorCommandResult::Failure("Could not set the values of " + who + ": " + error);
            }
            EditorCommandResult result =
                EditorCommandResult::Success("Set " + std::to_string(values.GetArraySize()) + " value(s) of " + who);
            result.undo = MakeSetManyCall(*actor, olds);
            result.redo = MakeSetManyCall(*actor, values);
            return result;
        };
        return command;
    }

    namespace
    {
        // actor.destroy の呼び出し(actor.restore の Undo、actor.destroy の Redo 用)。アクターは guid で指す。
        EditorCommandCall MakeDestroyCall(const core::Guid& guid)
        {
            EditorCommandCall call;
            call.name = "actor.destroy";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("actor", engine::ArchiveValue(guid.ToString()));
            return call;
        }

        // actor.restore の呼び出し(actor.destroy の Undo、actor.restore の Redo 用)。data は控えたアクター(と子孫)。
        // parentGuid は消す前の親(親なしなら空)。控えの外なので data には入らず、戻すときに guid で付け直す(HIER-2a)。
        EditorCommandCall MakeRestoreCall(const engine::ArchiveValue& data, const std::string& parentGuid)
        {
            EditorCommandCall call;
            call.name = "actor.restore";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("data", data);
            if (!parentGuid.empty())
            {
                call.args.Set("parent", engine::ArchiveValue(parentGuid));
            }
            return call;
        }

        // 控え(シーンのファイルと同じ形。最初のアクターが消したもの、後ろはその子孫)から、最初のアクターの guid を読む。
        // 形: {"objects": [{"id": 0, "properties": {"guid": "..."}}, ...], "actors": [0]}。読めなければ false。
        bool ReadSavedActorGuid(const engine::ArchiveValue& data, core::Guid& out)
        {
            const engine::ArchiveValue* actors = data.Find("actors");
            const engine::ArchiveValue* objects = data.Find("objects");
            if (actors == nullptr || objects == nullptr || actors->GetType() != engine::ArchiveValue::Type::Array ||
                objects->GetType() != engine::ArchiveValue::Type::Array || actors->GetArraySize() == 0)
            {
                return false;
            }
            const std::int64_t id = actors->At(0).AsInt(-1);
            for (std::size_t i = 0; i < objects->GetArraySize(); ++i)
            {
                const engine::ArchiveValue& object = objects->At(i);
                const engine::ArchiveValue* objectId = object.Find("id");
                if (objectId == nullptr || objectId->AsInt(-1) != id)
                {
                    continue;
                }
                const engine::ArchiveValue* properties = object.Find("properties");
                const engine::ArchiveValue* guid = properties != nullptr ? properties->Find("guid") : nullptr;
                return guid != nullptr && guid->GetType() == engine::ArchiveValue::Type::String &&
                       core::Guid::Parse(guid->AsString(), out);
            }
            return false;
        }
    }

    EditorCommand MakeDestroyActorCommand()
    {
        EditorCommand command;
        command.name = "actor.destroy";
        command.label = "Destroy actor";
        command.description = "Remove an actor (with its components and child actors) from the scene. "
                              "The actor can be given by name (if unique) or by guid. Can be undone (editor.undo).";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        command.args = {actorArg};
        command.execute = [](EditorCommandContext& context) {
            engine::Scene& scene = context.app.GetScene();
            std::string error;
            engine::Actor* actor = FindActorFromArg(scene, context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            // 子孫も一緒に消す(HIER-2a)。親が先になる順で集める。
            std::vector<engine::Actor*> targets = {actor};
            actor->GetDescendants(targets);
            // 親は控えの外なので、控える前に外しておく(外を指すハンドルとして警告が出ないように)。親は guid で別に覚え、
            // Undo のときに付け直す。どうせこの後に消すので、外したままでよい。
            const engine::Actor* oldParent = actor->GetParent();
            const std::string parentGuid = oldParent != nullptr ? oldParent->guid.ToString() : std::string();
            actor->SetParent(nullptr, false);
            // 消す前に、シーンのファイルと同じ形で控える(guid、プレハブとの違い、コンポーネント、子孫が入る)。
            // Undo はこの控えを actor.restore に渡し、同じ guid のアクターを作り直す。
            engine::SceneSerializer::WriteResult saved = engine::SceneSerializer::WriteActors(scene, targets);
            const std::string who = DescribeActor(*actor);
            const core::Guid guid = actor->guid;
            // フレームの終わりに一覧から外れる(コンポーネントもそのときに消える)。子孫もその場で印を付ける
            // (フレームを回さない CLI が、この後すぐに保存するため)
            for (engine::Actor* target : targets)
            {
                target->Destroy();
            }
            std::string message = "Destroyed " + who;
            if (targets.size() > 1)
            {
                message += " and " + std::to_string(targets.size() - 1) + " child actor(s)";
            }
            EditorCommandResult result = EditorCommandResult::Success(message);
            // 外を指すハンドル(カメラの追う相手など)は控えに入らない。戻しても null になることを人に知らせる。
            result.details = std::move(saved.warnings);
            result.undo = MakeRestoreCall(saved.value, parentGuid);
            result.redo = MakeDestroyCall(guid);
            return result;
        };
        return command;
    }

    EditorCommand MakeRestoreActorCommand()
    {
        EditorCommand command;
        command.name = "actor.restore";
        command.label = "Restore actor";
        command.description = "Put back an actor (and its child actors) saved by actor.destroy (used by editor.undo), with the same guid. "
                              "The actors are added at the end of the scene list.";
        command.modifiesScene = true;
        command.args = {
            {"data", EditorArgType::Any,
             "Actors in the same form as a scene file ({\"objects\": [...], \"actors\": [id, ...]}). The first one is the restored actor (with its guid), the rest are its descendants", true},
            {"parent", EditorArgType::String, "Guid of the parent to attach the restored actor to (leave out for none)", false},
        };
        command.execute = [](EditorCommandContext& context) {
            engine::Scene& scene = context.app.GetScene();
            const engine::ArchiveValue& data = *context.args.Find("data");
            core::Guid guid;
            if (!ReadSavedActorGuid(data, guid))
            {
                return EditorCommandResult::Failure("\"data\" is not one saved actor with a guid");
            }
            // 同じ guid が生きていれば、読み込みが guid を振り直してしまい、Undo の履歴の guid と食い違う。先に断る。
            if (engine::Actor* alive = scene.FindActorByGuid(guid))
            {
                return EditorCommandResult::Failure("The actor is already in the scene: " + DescribeActor(*alive));
            }
            // シーンを読むときと同じ道具で作る。プレハブから置いたものは、プレハブから作ってから違いを重ねる。
            // プレハブの探し方はシーンのファイルを読むときと同じ(Application::FindPrefabForScene)。
            engine::Application& app = context.app;
            engine::SceneSerializer::ReadReport report = engine::SceneSerializer::Read(
                data, scene, app.GetTypeRegistry(),
                [&app](const core::Guid& prefabGuid) { return app.FindPrefabForScene(prefabGuid); });
            engine::Actor* actor = scene.FindActorByGuid(guid);
            if (report.addedActorCount == 0 || actor == nullptr)
            {
                EditorCommandResult failed = EditorCommandResult::Failure("Could not restore the actor");
                failed.details = std::move(report.warnings);
                return failed;
            }
            // 消す前の親に付け直す(見える場所は、控えたローカルの値のまま親から見た値として使う)
            std::string parentGuid;
            if (const engine::ArchiveValue* parentArg = context.args.Find("parent"))
            {
                parentGuid = parentArg->AsString();
                core::Guid parentId;
                engine::Actor* parentActor =
                    core::Guid::Parse(parentGuid, parentId) ? scene.FindActorByGuid(parentId) : nullptr;
                if (parentActor == nullptr || !actor->SetParent(parentActor, false))
                {
                    report.warnings.push_back("Could not attach to the parent " + parentGuid + "; the actor has no parent now");
                }
            }
            EditorCommandResult result = EditorCommandResult::Success("Restored " + DescribeActor(*actor));
            result.details = std::move(report.warnings);
            result.undo = MakeDestroyCall(guid);
            result.redo = MakeRestoreCall(data, parentGuid);
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("guid", engine::ArchiveValue(guid.ToString()));
            return result;
        };
        return command;
    }

    // ================= 親子(HIER-3) =================

    namespace
    {
        // Transform を引数の値({"position": {x, y, z}, "rotation", "scale": {x, y, z}})にする。
        engine::ArchiveValue MakeTransformArg(const engine::Transform& transform)
        {
            auto vector3 = [](const core::Vector3& value) {
                engine::ArchiveValue out = engine::ArchiveValue::MakeObject();
                out.Set("x", engine::ArchiveValue(static_cast<double>(value.x)));
                out.Set("y", engine::ArchiveValue(static_cast<double>(value.y)));
                out.Set("z", engine::ArchiveValue(static_cast<double>(value.z)));
                return out;
            };
            engine::ArchiveValue value = engine::ArchiveValue::MakeObject();
            value.Set("position", vector3(transform.position));
            value.Set("rotation", engine::ArchiveValue(static_cast<double>(transform.rotation)));
            value.Set("scale", vector3(transform.scale));
            return value;
        }

        // MakeTransformArg の形から Transform を読む。書いていない値は、今の値(out の値)のまま。
        void ReadTransformArg(const engine::ArchiveValue& value, engine::Transform& out)
        {
            auto vector3 = [](const engine::ArchiveValue* from, core::Vector3& to) {
                if (from == nullptr)
                {
                    return;
                }
                to.x = static_cast<float>(from->Find("x") != nullptr ? from->Find("x")->AsFloat(to.x) : to.x);
                to.y = static_cast<float>(from->Find("y") != nullptr ? from->Find("y")->AsFloat(to.y) : to.y);
                to.z = static_cast<float>(from->Find("z") != nullptr ? from->Find("z")->AsFloat(to.z) : to.z);
            };
            vector3(value.Find("position"), out.position);
            if (const engine::ArchiveValue* rotation = value.Find("rotation"))
            {
                out.rotation = static_cast<float>(rotation->AsFloat(out.rotation));
            }
            vector3(value.Find("scale"), out.scale);
        }

        // actor.setParent の呼び出し(Undo / Redo 用)。親は guid(親なしなら空)。transform は付け替えた後のローカルの値。
        EditorCommandCall MakeSetParentCall(const core::Guid& actor, const std::string& parentGuid,
                                            const engine::Transform& transform)
        {
            EditorCommandCall call;
            call.name = "actor.setParent";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("actor", engine::ArchiveValue(actor.ToString()));
            call.args.Set("parent", engine::ArchiveValue(parentGuid));
            call.args.Set("keepWorld", engine::ArchiveValue(false));
            call.args.Set("transform", MakeTransformArg(transform));
            return call;
        }
    }

    EditorCommand MakeSetParentCommand()
    {
        EditorCommand command;
        command.name = "actor.setParent";
        command.label = "Set parent";
        command.description = "Make an actor the child of another actor, or give it no parent (leave out \"parent\" or give \"\"). "
                              "By default it stays where it is on the screen (its transform becomes relative to the new parent). "
                              "Refused if it would make a loop, or if the actor has a Rigidbody2D. Can be undone (editor.undo).";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor to move", true};
        actorArg.choices = ActorChoices;
        EditorCommandArg parentArg{"parent", EditorArgType::String, "Name (if unique) or guid of the new parent (\"\" for none)",
                                   false};
        parentArg.choices = ActorChoices;
        EditorCommandArg keepArg{"keepWorld", EditorArgType::Bool,
                                 "Keep the place on the screen (true), or keep the transform values as they are (false)", false};
        keepArg.defaultValue = engine::ArchiveValue(true);
        command.args = {
            actorArg,
            parentArg,
            keepArg,
            {"transform", EditorArgType::Any,
             "Local transform to set after changing the parent ({\"position\": {...}, \"rotation\", \"scale\": {...}}). Used by undo",
             false},
        };
        command.execute = [](EditorCommandContext& context) {
            engine::Scene& scene = context.app.GetScene();
            std::string error;
            engine::Actor* actor = FindActorFromArg(scene, context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            engine::Actor* parent = nullptr;
            const engine::ArchiveValue* parentArg = context.args.Find("parent");
            if (parentArg != nullptr && !parentArg->AsString().empty())
            {
                parent = FindActorFromArg(scene, parentArg->AsString(), error);
                if (parent == nullptr)
                {
                    return EditorCommandResult::Failure(error);
                }
            }
            const engine::ArchiveValue* keepArg = context.args.Find("keepWorld");
            const bool keepWorld = keepArg == nullptr || keepArg->AsBool(true);

            // Undo のために、前の親と、前のローカルの値を控える
            const engine::Actor* oldParent = actor->GetParent();
            const std::string oldParentGuid = oldParent != nullptr ? oldParent->guid.ToString() : std::string();
            const engine::Transform oldTransform = actor->transform;
            if (oldParent == parent && context.args.Find("transform") == nullptr)
            {
                return EditorCommandResult::Failure(DescribeActor(*actor) + " already has that parent");
            }
            const char* refused = nullptr;
            if (!actor->SetParent(parent, keepWorld, &refused))
            {
                return EditorCommandResult::Failure(std::string("Could not set the parent: ") +
                                                    (refused != nullptr ? refused : "unknown reason"));
            }
            if (const engine::ArchiveValue* transformArg = context.args.Find("transform"))
            {
                ReadTransformArg(*transformArg, actor->transform);
            }
            EditorCommandResult result = EditorCommandResult::Success(
                parent != nullptr ? "Set the parent of " + DescribeActor(*actor) + " to " + DescribeActor(*parent)
                                  : "Removed the parent of " + DescribeActor(*actor));
            result.undo = MakeSetParentCall(actor->guid, oldParentGuid, oldTransform);
            result.redo = MakeSetParentCall(actor->guid, parent != nullptr ? parent->guid.ToString() : std::string(),
                                            actor->transform);
            return result;
        };
        return command;
    }

    namespace
    {
        // コンポーネント1つの控え(component.remove の Undo、component.add の Redo 用)。形:
        //   {"value": {"type": "...", "properties": {...}}, "references": ["<guid>", null, ...]}
        // ハンドルのプロパティは、メモリの中の値(表の場所と世代)のままでは、控えた後にアクターが作り直されると意味を失う。
        // そこで、相手がアクターなら references に guid を並べ、ハンドルにはその番号を書く(シーンのファイルの番号と同じ考え方)。
        // 戻すときは guid でアクターを探し直すので、相手が Undo で作り直されていても(guid は変わらない)つながる。
        // アクター以外(ほかのアクターのコンポーネントなど)を指すハンドルは控えられず、null になる(warnings に書く)。
        engine::ArchiveValue SaveComponent(engine::Application& app, const engine::Component& component,
                                           std::vector<std::string>& warnings)
        {
            engine::ArchiveValue references = engine::ArchiveValue::MakeArray();
            // [&]: 外の変数(references など)を参照で取り込むラムダ式。Write の中で、ハンドルのプロパティごとに呼ばれる。
            engine::ArchiveValue value = engine::ObjectSerializer::Write(component, [&](core::ObjectHandle handle) -> std::int64_t {
                if (handle.IsNull())
                {
                    return -1; // 何も指していない → null のまま
                }
                if (const engine::Actor* actor = app.GetRegistry().Resolve<engine::Actor>(handle))
                {
                    references.Append(engine::ArchiveValue(actor->guid.ToString()));
                    return static_cast<std::int64_t>(references.GetArraySize() - 1);
                }
                const core::Object* object = app.GetRegistry().Resolve<core::Object>(handle);
                if (object != nullptr)
                {
                    warnings.push_back("A reference to a " + std::string(object->GetType().GetName()) +
                                       " is not kept (it will be null when put back)");
                }
                return -1;
            });
            engine::ArchiveValue saved = engine::ArchiveValue::MakeObject();
            saved.Set("value", std::move(value));
            saved.Set("references", std::move(references));
            return saved;
        }

        // SaveComponent の控えを component に書く。ハンドルは references の guid でアクターを探し直す。
        // 読めなかったもの(知らない名前、相手のアクターがもういない、など)は warnings に書く。
        void LoadComponent(engine::Application& app, const engine::ArchiveValue& saved, engine::Component& component,
                           std::vector<std::string>& warnings)
        {
            const engine::ArchiveValue* value = saved.Find("value");
            const engine::ArchiveValue* references = saved.Find("references");
            if (value == nullptr)
            {
                warnings.push_back("\"data\" has no \"value\"; the component keeps its default values");
                return;
            }
            engine::Scene& scene = app.GetScene();
            engine::ObjectSerializer::ReadReport report = engine::ObjectSerializer::ReadProperties(
                *value, component, [&](std::int64_t id) -> core::ObjectHandle {
                    if (references == nullptr || references->GetType() != engine::ArchiveValue::Type::Array || id < 0 ||
                        static_cast<std::size_t>(id) >= references->GetArraySize())
                    {
                        return core::ObjectHandle();
                    }
                    core::Guid guid;
                    if (!core::Guid::Parse(references->At(static_cast<std::size_t>(id)).AsString(), guid))
                    {
                        return core::ObjectHandle();
                    }
                    if (engine::Actor* actor = scene.FindActorByGuid(guid))
                    {
                        return actor->GetHandle();
                    }
                    warnings.push_back("The actor " + guid.ToString() + " is no longer in the scene (the reference is null)");
                    return core::ObjectHandle();
                });
            for (std::string& warning : report.warnings)
            {
                warnings.push_back(std::move(warning));
            }
        }

        // component.remove の呼び出し(component.add の Undo、component.remove の Redo 用)。アクターは guid、コンポーネントは位置で指す。
        EditorCommandCall MakeRemoveComponentCall(const core::Guid& actor, const std::string& type, std::size_t index)
        {
            EditorCommandCall call;
            call.name = "component.remove";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("actor", engine::ArchiveValue(actor.ToString()));
            call.args.Set("component", engine::ArchiveValue(type));
            call.args.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(index)));
            return call;
        }

        // component.add の呼び出し(component.remove の Undo、component.add の Redo 用)。data は SaveComponent の控え。
        EditorCommandCall MakeAddComponentCall(const core::Guid& actor, const std::string& type, std::size_t index,
                                               const engine::ArchiveValue& data)
        {
            EditorCommandCall call;
            call.name = "component.add";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("actor", engine::ArchiveValue(actor.ToString()));
            call.args.Set("type", engine::ArchiveValue(type));
            call.args.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(index)));
            call.args.Set("data", data);
            return call;
        }

        // component.add の中身(prefab.revertComponent も使う)。typeName の型のコンポーネントを作り、data(SaveComponent の形。
        // 省略可)の値を書いて、actor の一覧の index 番目に入れる。Undo は入れた位置の component.remove。
        EditorCommandResult AddComponentToActor(engine::Application& app, engine::Actor& actor, const std::string& typeName,
                                                std::size_t index, const engine::ArchiveValue* data)
        {
            // 型の名前から型を探し、コンポーネントの子孫かを確かめてから作る(actor.spawn の type と同じ流れ)。
            const core::TypeRegistry& types = app.GetTypeRegistry();
            const core::TypeInfo* type = types.Find(typeName);
            if (type == nullptr)
            {
                return EditorCommandResult::Failure("Unknown type: " + typeName);
            }
            if (!type->IsA(engine::Component::StaticType()))
            {
                return EditorCommandResult::Failure(typeName + " is not a component");
            }
            engine::Component* component = core::Cast<engine::Component>(types.Create(typeName, app.GetRegistry()));
            if (component == nullptr)
            {
                return EditorCommandResult::Failure("Could not create " + typeName + " (out of object memory?)");
            }
            std::vector<std::string> warnings;
            if (data != nullptr)
            {
                // 控えから戻す(Undo): 値も prefabComponentId も控えのとおり。プレハブとの組が元どおりになる。
                LoadComponent(app, *data, *component, warnings);
            }
            else if (engine::Collider2D* collider = core::Cast<engine::Collider2D>(component))
            {
                // 新しく足したコライダーは、同じアクターの絵の大きさと位置に合わせる(Unity と同じ)。
                // 控えから戻すとき(Undo / Redo)は、控えた値のままにする(上の分岐)。
                if (engine::FitColliderToSprite(*collider, actor))
                {
                    warnings.push_back("Fitted to the sprite");
                }
            }
            const std::size_t at = actor.InsertComponent(*component, index);

            EditorCommandResult result = EditorCommandResult::Success("Added " + typeName + " to " + DescribeActor(actor));
            result.details = std::move(warnings);
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(at)));
            // Undo は、入れた位置のものを外す。Redo は、足した直後の控えで同じ位置に足し直す(値と ID がそろう)。
            std::vector<std::string> ignored; // 足した直後の控えの警告は、上の details と同じことしか言わない
            result.undo = MakeRemoveComponentCall(actor.guid, typeName, at);
            result.redo = MakeAddComponentCall(actor.guid, typeName, at, SaveComponent(app, *component, ignored));
            return result;
        }

        // コンポーネントの型(Component の子孫)を選ばせる選択肢。型の登録簿(エンジンの型と、ゲームが登録した型)から作る。
        std::vector<EditorArgChoice> ComponentTypeChoices(engine::Application& app, const engine::ArchiveValue&)
        {
            std::vector<EditorArgChoice> names;
            const core::TypeRegistry& types = app.GetTypeRegistry();
            for (std::size_t i = 0; i < types.GetCount(); ++i)
            {
                const core::TypeInfo* type = types.GetType(i);
                if (type != nullptr && type->IsA(engine::Component::StaticType()))
                {
                    names.push_back({type->GetName(), std::string()});
                }
            }
            return names;
        }

        // 選んだアクターに付いているコンポーネントの型を選ばせる選択肢。
        std::vector<EditorArgChoice> AttachedComponentChoices(engine::Application& app, const engine::ArchiveValue& args)
        {
            std::vector<EditorArgChoice> names;
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string error;
            const engine::Actor* actor = actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), error) : nullptr;
            if (actor == nullptr)
            {
                return names;
            }
            for (const core::ObjectHandle handle : actor->components)
            {
                const engine::Component* component = app.GetRegistry().Resolve<engine::Component>(handle);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    names.push_back({component->GetType().GetName(), std::string()});
                }
            }
            return names;
        }
    }

    EditorCommand MakeAddComponentCommand()
    {
        EditorCommand command;
        command.name = "component.add";
        command.label = "Add component";
        command.description = "Attach a new component of the given type to an actor. Can be undone (editor.undo).";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        EditorCommandArg typeArg{"type", EditorArgType::String, "Type name of the component, like \"SpriteComponent\"", true};
        typeArg.choices = ComponentTypeChoices;
        command.args = {
            actorArg,
            typeArg,
            {"index", EditorArgType::Int, "Position in the actor's component list (leave out to add at the end)", false},
            {"data", EditorArgType::Any, "Values saved by component.remove (used by editor.undo)", false},
        };
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            std::string error;
            engine::Actor* actor = FindActorFromArg(app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const engine::ArchiveValue* indexValue = context.args.Find("index");
            if (indexValue != nullptr && indexValue->AsInt(-1) < 0)
            {
                return EditorCommandResult::Failure("\"index\" must be 0 or more");
            }
            const std::size_t index = indexValue != nullptr ? static_cast<std::size_t>(indexValue->AsInt()) : actor->components.size();
            return AddComponentToActor(app, *actor, context.args.Find("type")->AsString(), index, context.args.Find("data"));
        };
        return command;
    }

    EditorCommand MakeRemoveComponentCommand()
    {
        EditorCommand command;
        command.name = "component.remove";
        command.label = "Remove component";
        command.description = "Detach a component from an actor (the first one of the given type, or the one at index). "
                              "Can be undone (editor.undo): it comes back at the same position with the same values.";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        EditorCommandArg componentArg{"component", EditorArgType::String, "Type name of a component on the actor", true};
        componentArg.choices = AttachedComponentChoices;
        command.args = {
            actorArg,
            componentArg,
            {"index", EditorArgType::Int,
             "Position in the actor's component list (leave out for the first one of the type; used by editor.undo)", false},
        };
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            std::string error;
            engine::Actor* actor = FindActorFromArg(app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string& typeName = context.args.Find("component")->AsString();
            engine::Component* component = nullptr;
            if (const engine::ArchiveValue* indexValue = context.args.Find("index"))
            {
                // 位置で指す(Undo / Redo)。その位置にあるものが、言われた型でなければ断る(履歴と食い違っている)。
                const std::int64_t index = indexValue->AsInt(-1);
                const core::TypeInfo* type = app.GetTypeRegistry().Find(typeName);
                if (index >= 0 && static_cast<std::size_t>(index) < actor->components.size())
                {
                    component = app.GetRegistry().Resolve<engine::Component>(actor->components[static_cast<std::size_t>(index)]);
                }
                if (component == nullptr || component->IsPendingDestroy() || type == nullptr || !component->GetType().IsA(*type))
                {
                    return EditorCommandResult::Failure(DescribeActor(*actor) + " has no " + typeName + " at index " +
                                                        std::to_string(index));
                }
            }
            else
            {
                component = FindComponentFromArg(app, *actor, typeName, error);
                if (component == nullptr)
                {
                    return EditorCommandResult::Failure(error);
                }
            }
            // 外す前に控える(型の名前は、指された名前ではなく本当の型。子孫の型を親の名前で指したときも、同じ型で戻すため)。
            const std::string actualType = component->GetType().GetName();
            std::vector<std::string> warnings;
            const engine::ArchiveValue saved = SaveComponent(app, *component, warnings);
            const std::int64_t removedAt = actor->RemoveComponent(*component);
            if (removedAt < 0)
            {
                return EditorCommandResult::Failure("The component is not in " + DescribeActor(*actor) + "'s list");
            }
            EditorCommandResult result = EditorCommandResult::Success("Removed " + actualType + " from " + DescribeActor(*actor));
            result.details = std::move(warnings);
            result.undo = MakeAddComponentCall(actor->guid, actualType, static_cast<std::size_t>(removedAt), saved);
            result.redo = MakeRemoveComponentCall(actor->guid, actualType, static_cast<std::size_t>(removedAt));
            return result;
        };
        return command;
    }

    namespace
    {
        // プレハブとの違い1つを、人が読む1行にする("SpriteComponent.color.r = 0.74 (prefab: 1)")。
        std::string DescribeOverride(const engine::Prefab::Override& item)
        {
            std::string value;
            std::string prefabValue;
            engine::JsonFormat::Write(item.value, value, false);
            engine::JsonFormat::Write(item.prefabValue, prefabValue, false);
            return (item.component.empty() ? std::string() : item.component + ".") + item.path + " = " + value +
                   " (prefab: " + prefabValue + ")" + (item.perInstance ? " [per instance]" : "");
        }

        // 引数の actor が指すアクターの、プレハブとの違い(選択肢を作るため。見つからなければ空)。
        engine::Prefab::OverridesResult OverridesFromArgs(engine::Application& app, const engine::ArchiveValue& args)
        {
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string ignored;
            const engine::Actor* actor =
                actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), ignored) : nullptr;
            return actor != nullptr ? engine::Prefab::FindOverrides(*actor, &app.GetTypeRegistry()) : engine::Prefab::OverridesResult{};
        }

        // プレハブから置いたアクターだけを選択肢にする。
        std::vector<EditorArgChoice> PrefabInstanceChoices(engine::Application& app, const engine::ArchiveValue& args)
        {
            std::vector<EditorArgChoice> choices = ActorChoices(app, args);
            std::vector<EditorArgChoice> instances;
            for (EditorArgChoice& choice : choices)
            {
                core::Guid guid;
                const engine::Actor* actor =
                    core::Guid::Parse(choice.value, guid) ? app.GetScene().FindActorByGuid(guid) : nullptr;
                if (actor != nullptr && !actor->prefab.IsNull())
                {
                    instances.push_back(std::move(choice));
                }
            }
            return instances;
        }

        // 引数の component と property(どちらも省略可)に当てはまる違いを選ぶ。
        //   property を指したとき: その道筋と、その中(構造体を指せば、中のメンバー全部)。1体ずつ違う値も含む
        //   property を省いたとき: 全部。ただし、1体ずつ違う値(名前、位置、回転)は飛ばす(Unity の Revert All / Apply All と同じ)
        std::vector<const engine::Prefab::Override*> SelectOverrides(const engine::Prefab::OverridesResult& found,
                                                                     const engine::ArchiveValue& args)
        {
            const engine::ArchiveValue* componentValue = args.Find("component");
            const engine::ArchiveValue* propertyValue = args.Find("property");
            const std::string component = componentValue != nullptr ? componentValue->AsString() : std::string();
            std::vector<const engine::Prefab::Override*> selected;
            for (const engine::Prefab::Override& item : found.overrides)
            {
                if (propertyValue == nullptr)
                {
                    if (!item.perInstance && (componentValue == nullptr || item.component == component))
                    {
                        selected.push_back(&item);
                    }
                    continue;
                }
                const std::string& property = propertyValue->AsString();
                if (item.component == component &&
                    (item.path == property || item.path.rfind(property + ".", 0) == 0))
                {
                    selected.push_back(&item);
                }
            }
            return selected;
        }

        // prefab.revert と prefab.apply の引数(actor、component、property)。選択肢は、選んだアクターの違いから作る。
        std::vector<EditorCommandArg> MakeOverrideArgs()
        {
            EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of an actor placed from a prefab", true};
            actorArg.choices = PrefabInstanceChoices;
            EditorCommandArg componentArg{"component", EditorArgType::String,
                                          "Type name of a component (leave out for the actor itself, or for all when property is left out)", false};
            componentArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
                std::vector<EditorArgChoice> choices;
                for (const engine::Prefab::Override& item : OverridesFromArgs(app, args).overrides)
                {
                    if (!item.component.empty() &&
                        std::none_of(choices.begin(), choices.end(),
                                     [&item](const EditorArgChoice& choice) { return choice.value == item.component; }))
                    {
                        choices.push_back({item.component, std::string()});
                    }
                }
                return choices;
            };
            EditorCommandArg propertyArg{"property", EditorArgType::String,
                                         "Path of one overridden value, like \"transform.position.x\" (leave out for all, except name, position and rotation)", false};
            propertyArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
                std::vector<EditorArgChoice> choices;
                const engine::ArchiveValue* componentValue = args.Find("component");
                const std::string component = componentValue != nullptr ? componentValue->AsString() : std::string();
                for (const engine::Prefab::Override& item : OverridesFromArgs(app, args).overrides)
                {
                    if (item.component == component)
                    {
                        choices.push_back({item.path, DescribeOverride(item)});
                    }
                }
                return choices;
            };
            return {actorArg, componentArg, propertyArg};
        }
    }

    EditorCommand MakeListOverridesCommand()
    {
        EditorCommand command;
        command.name = "prefab.overrides";
        command.label = "List overrides";
        command.description = "List the values of an actor that differ from its prefab (the values saved in the scene). "
                              "Values marked [per instance] (name, position, rotation) are left out of revert/apply all.";
        command.readsScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of an actor placed from a prefab", true};
        actorArg.choices = PrefabInstanceChoices;
        command.args = {actorArg};
        command.execute = [](EditorCommandContext& context) {
            std::string error;
            engine::Actor* actor = FindActorFromArg(context.app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor, &context.app.GetTypeRegistry());
            if (found.prefab == nullptr)
            {
                return EditorCommandResult::Failure(DescribeActor(*actor) + ": " + found.error);
            }
            const std::string* path = context.app.GetAssetDatabase().FindPath(found.prefab->GetGuid());
            EditorCommandResult result = EditorCommandResult::Success(
                std::to_string(found.overrides.size()) + " override(s) on " + DescribeActor(*actor) + " from " +
                (path != nullptr ? *path : found.prefab->GetGuid().ToString()));
            result.data = engine::ArchiveValue::MakeArray();
            for (const engine::Prefab::Override& item : found.overrides)
            {
                result.details.push_back(DescribeOverride(item));
                engine::ArchiveValue& entry = result.data.Append(engine::ArchiveValue::MakeObject());
                entry.Set("component", engine::ArchiveValue(item.component));
                entry.Set("property", engine::ArchiveValue(item.path));
                entry.Set("value", item.value);
                entry.Set("prefabValue", item.prefabValue);
                entry.Set("perInstance", engine::ArchiveValue(item.perInstance));
            }
            // 足した・外したコンポーネントは、人向けの行にだけ出す(data は値の違いの配列のまま)
            for (const engine::Prefab::ComponentChange& added : found.addedComponents)
            {
                result.details.push_back("+ " + added.type + " (added, not in the prefab)");
            }
            for (const engine::Prefab::ComponentChange& removed : found.removedComponents)
            {
                result.details.push_back("- " + removed.type + " (removed, still in the prefab)");
            }
            return result;
        };
        return command;
    }

    namespace
    {
        // プレハブの中のコンポーネントの記録を、component.add の data(SaveComponent の形)にする。
        // プレハブの中のハンドルは「プレハブのファイルの中の番号」なので、references をその番号で引ける表にする:
        // プレハブのアクターの番号には、置いたアクターの guid を入れる(自分のアクターを指すハンドルはつながる)。
        // ほかの番号は null(戻すとハンドルは null)。
        engine::ArchiveValue MakeDataFromPrefabEntry(const engine::ArchiveValue& prefabData, const engine::ArchiveValue& entry,
                                                     const engine::Actor& actor)
        {
            engine::ArchiveValue references = engine::ArchiveValue::MakeArray();
            // 置いたものに当たる、プレハブの中のアクターの番号(HIER-2b。子なら子の番号)
            const std::int64_t actorId = engine::SceneSerializer::FindPrefabActorNumber(prefabData, actor.prefabActorId);
            for (std::int64_t id = 0; id <= actorId; ++id)
            {
                references.Append(id == actorId ? engine::ArchiveValue(actor.guid.ToString()) : engine::ArchiveValue());
            }
            engine::ArchiveValue data = engine::ArchiveValue::MakeObject();
            data.Set("value", entry);
            data.Set("references", std::move(references));
            return data;
        }

        // 選んだアクターの、置いた後に外したコンポーネントを選ばせる選択肢(値は ID、表示は型の名前)。
        std::vector<EditorArgChoice> RemovedComponentChoices(engine::Application& app, const engine::ArchiveValue& args)
        {
            std::vector<EditorArgChoice> choices;
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string error;
            const engine::Actor* actor = actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), error) : nullptr;
            if (actor == nullptr)
            {
                return choices;
            }
            for (const engine::Prefab::ComponentChange& removed : engine::Prefab::FindOverrides(*actor, &app.GetTypeRegistry()).removedComponents)
            {
                choices.push_back({removed.componentId.ToString(), removed.type});
            }
            return choices;
        }
    }

    EditorCommand MakeRevertRemovedComponentCommand()
    {
        EditorCommand command;
        command.name = "prefab.revertComponent";
        command.label = "Revert removed component";
        command.description = "Put back a component that was removed from an actor placed from a prefab, with the prefab's "
                              "values and id, so it follows the prefab again. (To revert an added component, use "
                              "component.remove.) Can be undone (editor.undo).";
        command.modifiesScene = true;
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of an actor placed from a prefab", true};
        actorArg.choices = PrefabInstanceChoices;
        EditorCommandArg idArg{"componentId", EditorArgType::String, "Id (in the prefab) of the removed component", true};
        idArg.choices = RemovedComponentChoices;
        command.args = {actorArg, idArg};
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            std::string error;
            engine::Actor* actor = FindActorFromArg(app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string who = DescribeActor(*actor);
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor, &context.app.GetTypeRegistry());
            if (found.prefab == nullptr)
            {
                return EditorCommandResult::Failure(who + ": " + found.error);
            }
            const std::string& idText = context.args.Find("componentId")->AsString();
            core::Guid id;
            if (!core::Guid::Parse(idText, id))
            {
                return EditorCommandResult::Failure("\"componentId\" is not a guid: " + idText);
            }
            for (const engine::Prefab::ComponentChange& removed : found.removedComponents)
            {
                if (removed.componentId != id)
                {
                    continue;
                }
                // 外したものを、プレハブの値と ID で作り直す。中身は component.add と同じ(Undo は component.remove)。
                const engine::ArchiveValue data = MakeDataFromPrefabEntry(found.prefab->GetData(), removed.prefabEntry, *actor);
                EditorCommandResult result = AddComponentToActor(app, *actor, removed.type, removed.index, &data);
                if (result.succeeded)
                {
                    result.message = "Reverted removed " + removed.type + " on " + who;
                }
                return result;
            }
            return EditorCommandResult::Failure(who + " has no removed component with id " + idText);
        };
        return command;
    }

    namespace
    {
        // prefab.revert / prefab.apply で扱う1体: アクターと、そのプレハブとの違いと、選んだ違い。
        struct OverrideTarget
        {
            engine::Actor* actor = nullptr;
            engine::Prefab::OverridesResult found;
            std::vector<const engine::Prefab::Override*> selected; // found.overrides の中を指す
        };

        // actor と、全部を選ぶとき(property も component も指していない)は、同じプレハブから置いた子孫も集める(HIER)。
        // 子孫は、プレハブの中の ID を持つものだけ(置いた後に足した子は、プレハブに組が無いので入れない)。
        // 違いを読めないアクターがあれば、空の一覧と error。
        std::vector<OverrideTarget> CollectOverrideTargets(engine::Application& app, engine::Actor& actor,
                                                           const engine::ArchiveValue& args, std::string& error)
        {
            std::vector<engine::Actor*> actors = {&actor};
            if (args.Find("property") == nullptr && args.Find("component") == nullptr)
            {
                std::vector<engine::Actor*> descendants;
                actor.GetDescendants(descendants);
                for (engine::Actor* descendant : descendants)
                {
                    if (descendant->prefab.guid == actor.prefab.guid && !descendant->prefabActorId.IsNull())
                    {
                        actors.push_back(descendant);
                    }
                }
            }
            std::vector<OverrideTarget> targets(actors.size()); // 先に大きさを決める(後で中を指すので、並べ直さない)
            for (std::size_t i = 0; i < actors.size(); ++i)
            {
                targets[i].actor = actors[i];
                targets[i].found = engine::Prefab::FindOverrides(*actors[i], &app.GetTypeRegistry());
                if (targets[i].found.prefab == nullptr)
                {
                    error = DescribeActor(*actors[i]) + ": " + targets[i].found.error;
                    return {};
                }
                targets[i].selected = SelectOverrides(targets[i].found, args);
            }
            return targets;
        }

        // 選んだ違いの数(全員の合計)。
        std::size_t CountSelected(const std::vector<OverrideTarget>& targets)
        {
            std::size_t count = 0;
            for (const OverrideTarget& target : targets)
            {
                count += target.selected.size();
            }
            return count;
        }
    }

    EditorCommand MakeRevertPrefabCommand()
    {
        EditorCommand command;
        command.name = "prefab.revert";
        command.label = "Revert to prefab";
        command.description = "Throw away values of an actor that differ from its prefab, so they follow the prefab again. "
                              "Give a property to revert one value; leave it out to revert all except name, position and "
                              "rotation. Can be undone (editor.undo).";
        command.modifiesScene = true;
        command.args = MakeOverrideArgs();
        command.execute = [](EditorCommandContext& context) {
            std::string error;
            engine::Actor* actor = FindActorFromArg(context.app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string who = DescribeActor(*actor);
            const std::vector<OverrideTarget> targets = CollectOverrideTargets(context.app, *actor, context.args, error);
            if (targets.empty())
            {
                return EditorCommandResult::Failure(error);
            }
            const std::size_t count = CountSelected(targets);
            const engine::ArchiveValue* propertyValue = context.args.Find("property");
            if (count == 0)
            {
                if (propertyValue != nullptr)
                {
                    return EditorCommandResult::Failure(who + " has no override at '" + propertyValue->AsString() + "'");
                }
                return EditorCommandResult::Success("Nothing to revert on " + who); // 戻すものが無い(Undo の履歴にも積まない)
            }

            // プレハブの値を書く。property.setMany と同じ道具なので、Undo も property.setMany(前の値)の1件になる。
            // 子孫の値は、値ごとに "actor" を書いて、同じ1件に入れる。
            engine::ArchiveValue values = engine::ArchiveValue::MakeArray();
            for (const OverrideTarget& target : targets)
            {
                const std::string guid = target.actor == actor ? std::string() : target.actor->guid.ToString();
                for (const engine::Prefab::Override* item : target.selected)
                {
                    values.Append(MakeSetItem(guid, item->component, item->path, item->prefabValue));
                }
            }
            engine::ArchiveValue olds;
            if (!SetManyProperties(context.app, *actor, values, olds, error))
            {
                return EditorCommandResult::Failure("Could not revert " + who + ": " + error);
            }
            EditorCommandResult result = EditorCommandResult::Success(
                "Reverted " + std::to_string(count) + " value(s) of " + who +
                (targets.size() > 1 ? " and its child actors" : ""));
            for (const OverrideTarget& target : targets)
            {
                for (const engine::Prefab::Override* item : target.selected)
                {
                    result.details.push_back((target.actor == actor ? std::string() : DescribeActor(*target.actor) + ": ") +
                                             DescribeOverride(*item));
                }
            }
            result.undo = MakeSetManyCall(*actor, olds);
            result.redo = MakeSetManyCall(*actor, values);
            return result;
        };
        return command;
    }


    namespace
    {
        // プレハブの新しい値の木 newData をファイル(directory/prefabPath)に書き、新しいアセットとして読み直す
        // (読み込んだアセットは書き換えない。変わらないデータなので)。読み直したアセットのハンドルを reloaded に入れる。
        // prefab.apply と prefab.applyComponent が使う。
        bool WriteAndReloadPrefab(engine::Application& app, const std::string& directory, const std::string& prefabPath,
                                  const core::Guid& guid, const engine::ArchiveValue& newData, core::ObjectHandle& reloaded,
                                  std::string& error)
        {
            const std::string fullPath = directory + "/" + prefabPath;
            std::string text;
            if (!engine::JsonFormat::Write(newData, text, true) || !platform::WriteTextFile(fullPath.c_str(), text))
            {
                error = "Could not write " + fullPath;
                return false;
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            reloaded = loader.ReloadPrefab(guid);
            loader.WaitForAllLoads();
            const engine::PrefabAsset* newPrefab = loader.ResolvePrefab(reloaded);
            if (newPrefab == nullptr || !newPrefab->IsLoaded())
            {
                error = "Wrote " + prefabPath + " but could not load it again";
                return false;
            }
            return true;
        }
    }

    EditorCommand MakeApplyPrefabCommand(std::string assetsDirectory)
    {
        EditorCommand command;
        command.name = "prefab.apply";
        command.label = "Apply to prefab";
        command.description = "Write values of an actor that differ from its prefab into the prefab file, so every actor "
                              "placed from it follows (except ones that changed that value themselves). Give a property to "
                              "apply one value; leave it out to apply all except name, position and rotation. Cannot be undone "
                              "(it rewrites the prefab file).";
        command.modifiesScene = true; // 置いたものの値が変わる(CLI では、シーンも保存する)
        command.args = MakeOverrideArgs();
        command.execute = [directory = std::move(assetsDirectory)](EditorCommandContext& context) {
            engine::Application& app = context.app;
            std::string error;
            engine::Actor* actor = FindActorFromArg(app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string who = DescribeActor(*actor);
            const std::vector<OverrideTarget> targets = CollectOverrideTargets(app, *actor, context.args, error);
            if (targets.empty())
            {
                return EditorCommandResult::Failure(error);
            }
            const std::size_t count = CountSelected(targets);
            const engine::ArchiveValue* propertyValue = context.args.Find("property");
            if (count == 0)
            {
                if (propertyValue != nullptr)
                {
                    return EditorCommandResult::Failure(who + " has no override at '" + propertyValue->AsString() + "'");
                }
                return EditorCommandResult::Success("Nothing to apply from " + who);
            }
            const engine::Prefab::OverridesResult& found = targets[0].found;
            const core::Guid guid = found.prefab->GetGuid();
            const std::string* relativePath = app.GetAssetDatabase().FindPath(guid);
            if (relativePath == nullptr)
            {
                return EditorCommandResult::Failure("The prefab " + guid.ToString() + " is not in the asset table");
            }
            const std::string prefabPath = *relativePath; // 表を作り直しても消えないように写す

            // 1. 新しいプレハブの値を作り、ファイルに書く(読み込んだアセットは書き換えない。変わらないデータなので)
            //    アクターごとに、プレハブの中の同じ ID のアクターの記録へ書く(子孫の違いも。HIER)。
            std::vector<std::vector<engine::Prefab::ValueChange>> changesOf(targets.size());
            engine::ArchiveValue newData = found.prefab->GetData();
            for (std::size_t t = 0; t < targets.size(); ++t)
            {
                for (const engine::Prefab::Override* item : targets[t].selected)
                {
                    changesOf[t].push_back({item->component, item->path, item->prefabValue, item->value, item->componentId});
                }
                if (changesOf[t].empty())
                {
                    continue;
                }
                engine::ArchiveValue written;
                if (!engine::Prefab::WriteChanges(newData, changesOf[t], written, error, targets[t].actor->prefabActorId))
                {
                    return EditorCommandResult::Failure("Could not apply " + DescribeActor(*targets[t].actor) + " to " +
                                                        prefabPath + ": " + error);
                }
                newData = std::move(written);
            }
            // 2. ファイルから読み直す(新しいアセットになる)
            core::ObjectHandle reloaded;
            if (!WriteAndReloadPrefab(app, directory, prefabPath, guid, newData, reloaded, error))
            {
                return EditorCommandResult::Failure(error);
            }

            // 3. 同じプレハブから置いた全員に伝える: 古いプレハブの値のままのものだけが、新しい値になる。
            //    全員が新しいアセットを指すようにする(次の保存では、新しいプレハブと比べた違いになる)。
            std::size_t followed = 0;
            app.GetScene().ForEachActor([&](engine::Actor& other) {
                if (other.prefab.guid == guid)
                {
                    // プレハブの中の同じアクター(根なら根、子なら同じ ID の子)の違いだけを伝える(HIER-2b)
                    for (std::size_t t = 0; t < targets.size(); ++t)
                    {
                        if (other.prefabActorId == targets[t].actor->prefabActorId && !changesOf[t].empty() &&
                            engine::Prefab::FollowChanges(other, changesOf[t]) > 0)
                        {
                            ++followed;
                        }
                    }
                    other.prefab.asset = reloaded;
                }
            });

            EditorCommandResult result = EditorCommandResult::Success(
                "Applied " + std::to_string(count) + " value(s) of " + who + (targets.size() > 1 ? " and its child actors" : "") +
                " to " + prefabPath + "; " + std::to_string(followed) + " other actor(s) followed");
            for (const OverrideTarget& target : targets)
            {
                for (const engine::Prefab::Override* item : target.selected)
                {
                    result.details.push_back((target.actor == actor ? std::string() : DescribeActor(*target.actor) + ": ") +
                                             DescribeOverride(*item));
                }
            }
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("prefab", engine::ArchiveValue(prefabPath));
            result.data.Set("followed", engine::ArchiveValue(static_cast<std::int64_t>(followed)));
            return result;
        };
        return command;
    }
    namespace
    {
        // 選んだアクターの、置いた後に足したコンポーネントの型を選ばせる選択肢。
        std::vector<EditorArgChoice> AddedComponentChoices(engine::Application& app, const engine::ArchiveValue& args)
        {
            std::vector<EditorArgChoice> choices;
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string error;
            const engine::Actor* actor = actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), error) : nullptr;
            if (actor == nullptr)
            {
                return choices;
            }
            for (const engine::Prefab::ComponentChange& added : engine::Prefab::FindOverrides(*actor, &app.GetTypeRegistry()).addedComponents)
            {
                choices.push_back({added.type, std::string()});
            }
            return choices;
        }
    }

    EditorCommand MakeApplyComponentCommand(std::string assetsDirectory)
    {
        EditorCommand command;
        command.name = "prefab.applyComponent";
        command.label = "Apply component to prefab";
        command.description = "Write a component change of an actor into its prefab file, and pass it on to every actor placed "
                              "from the prefab. Give added (an added component's type: it is added to the prefab) or removed "
                              "(a removed component's id: it is removed from the prefab). Cannot be undone (it rewrites the "
                              "prefab file).";
        command.modifiesScene = true; // 置いたものにコンポーネントが増える・減る(CLI では、シーンも保存する)
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of an actor placed from a prefab", true};
        actorArg.choices = PrefabInstanceChoices;
        EditorCommandArg addedArg{"added", EditorArgType::String,
                                  "Type name of a component added after placing (the first such one)", false};
        addedArg.choices = AddedComponentChoices;
        EditorCommandArg removedArg{"removed", EditorArgType::String, "Id (in the prefab) of a component removed after placing",
                                    false};
        removedArg.choices = RemovedComponentChoices;
        command.args = {actorArg, addedArg, removedArg};
        command.execute = [directory = std::move(assetsDirectory)](EditorCommandContext& context) {
            engine::Application& app = context.app;
            std::string error;
            engine::Actor* actor = FindActorFromArg(app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const engine::ArchiveValue* addedValue = context.args.Find("added");
            const engine::ArchiveValue* removedValue = context.args.Find("removed");
            if ((addedValue == nullptr) == (removedValue == nullptr)) // 両方ある、または両方無い
            {
                return EditorCommandResult::Failure("Give exactly one of added or removed");
            }
            const std::string who = DescribeActor(*actor);
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor, &context.app.GetTypeRegistry());
            if (found.prefab == nullptr)
            {
                return EditorCommandResult::Failure(who + ": " + found.error);
            }
            const core::Guid prefabGuid = found.prefab->GetGuid();
            const std::string* relativePath = app.GetAssetDatabase().FindPath(prefabGuid);
            if (relativePath == nullptr)
            {
                return EditorCommandResult::Failure("The prefab " + prefabGuid.ToString() + " is not in the asset table");
            }
            const std::string prefabPath = *relativePath; // 表を作り直しても消えないように写す

            // 1. プレハブの新しい値の木を作る
            engine::ArchiveValue newData;
            std::vector<std::string> warnings;
            std::string what;            // 結果の1行に書く("added SpriteComponent" など)
            core::Guid componentId;      // 足す・消すコンポーネントのプレハブの中の ID
            engine::Component* applied = nullptr; // 足すとき: 置いたもののコンポーネント(新しい ID を覚えさせる)
            if (addedValue != nullptr)
            {
                const engine::Prefab::ComponentChange* added = nullptr;
                for (const engine::Prefab::ComponentChange& item : found.addedComponents)
                {
                    if (added == nullptr && item.type == addedValue->AsString())
                    {
                        added = &item;
                    }
                }
                applied = added != nullptr ? app.GetRegistry().Resolve<engine::Component>(actor->components[added->index]) : nullptr;
                if (applied == nullptr)
                {
                    return EditorCommandResult::Failure(who + " has no added component " + addedValue->AsString());
                }
                // 新しい ID を振り、プレハブの中で、置いたものと同じ並びの位置に入れる
                componentId = core::Guid::Generate();
                const engine::ArchiveValue entry =
                    engine::Prefab::MakeComponentEntry(*applied, found.prefab->GetData(), componentId, warnings);
                if (!engine::Prefab::AddComponentToData(found.prefab->GetData(), entry, added->prefabIndex, newData, error,
                                                        actor->prefabActorId))
                {
                    return EditorCommandResult::Failure("Could not apply to " + prefabPath + ": " + error);
                }
                what = "added " + added->type;
            }
            else
            {
                if (!core::Guid::Parse(removedValue->AsString(), componentId))
                {
                    return EditorCommandResult::Failure("\"removed\" is not a guid: " + removedValue->AsString());
                }
                std::string type;
                for (const engine::Prefab::ComponentChange& item : found.removedComponents)
                {
                    type = item.componentId == componentId ? item.type : type;
                }
                if (type.empty())
                {
                    return EditorCommandResult::Failure(who + " has no removed component with id " + removedValue->AsString());
                }
                if (!engine::Prefab::RemoveComponentFromData(found.prefab->GetData(), componentId, newData, error,
                                                             actor->prefabActorId))
                {
                    return EditorCommandResult::Failure("Could not apply to " + prefabPath + ": " + error);
                }
                what = "removed " + type;
            }

            // 2. ファイルに書いて、読み直す
            core::ObjectHandle reloaded;
            if (!WriteAndReloadPrefab(app, directory, prefabPath, prefabGuid, newData, reloaded, error))
            {
                return EditorCommandResult::Failure(error);
            }
            const engine::PrefabAsset* newPrefab = app.GetAssetLoader().ResolvePrefab(reloaded);
            if (applied != nullptr)
            {
                applied->prefabComponentId = componentId; // 置いたもののコンポーネントは、プレハブの新しいものと組になる
            }

            // 3. 同じプレハブから置いた全員に伝える(全員が新しいアセットを指すようにしてから)。
            //    足したとき: ほかの全員に、プレハブの値で同じコンポーネントを足す(新しいプレハブと比べると「外したもの」に見えるので、
            //    prefab.revertComponent と同じ道具で戻す)。消したとき: その ID のコンポーネントを持つ全員から外す。
            std::size_t followed = 0;
            std::vector<engine::Actor*> instances;
            app.GetScene().ForEachActor([&](engine::Actor& other) {
                if (other.prefab.guid == prefabGuid)
                {
                    other.prefab.asset = reloaded;
                    instances.push_back(&other);
                }
            });
            for (engine::Actor* other : instances)
            {
                if (other == actor)
                {
                    continue;
                }
                if (applied != nullptr)
                {
                    for (const engine::Prefab::ComponentChange& missing : engine::Prefab::FindOverrides(*other, &app.GetTypeRegistry()).removedComponents)
                    {
                        if (missing.componentId == componentId)
                        {
                            const engine::ArchiveValue data = MakeDataFromPrefabEntry(newPrefab->GetData(), missing.prefabEntry, *other);
                            if (AddComponentToActor(app, *other, missing.type, missing.index, &data).succeeded)
                            {
                                ++followed;
                            }
                        }
                    }
                    continue;
                }
                for (std::size_t i = 0; i < other->components.size(); ++i)
                {
                    engine::Component* component = app.GetRegistry().Resolve<engine::Component>(other->components[i]);
                    if (component != nullptr && !component->IsPendingDestroy() && component->prefabComponentId == componentId)
                    {
                        other->RemoveComponent(*component);
                        ++followed;
                        break;
                    }
                }
            }

            EditorCommandResult result = EditorCommandResult::Success("Applied (" + what + ") of " + who + " to " + prefabPath +
                                                                      "; " + std::to_string(followed) + " other actor(s) followed");
            result.details = std::move(warnings);
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("prefab", engine::ArchiveValue(prefabPath));
            result.data.Set("componentId", engine::ArchiveValue(componentId.ToString()));
            result.data.Set("followed", engine::ArchiveValue(static_cast<std::int64_t>(followed)));
            return result;
        };
        return command;
    }

    EditorCommand MakeCreatePrefabCommand(std::string assetsDirectory)
    {
        EditorCommand command;
        command.name = "prefab.create";
        command.label = "Create prefab";
        command.description = "Save an actor in the scene (with its components and values) as a new prefab file "
                              "in the assets folder, and give it a GUID (.meta). Does not overwrite existing files.";
        command.readsScene = true; // シーンは読むだけ(アクターはそのまま。プレハブにつながりもしない)
        EditorCommandArg actorArg{"actor", EditorArgType::String, "Actor to save: its name or guid", true};
        actorArg.choices = [](engine::Application& app, const engine::ArchiveValue&) {
            std::vector<EditorArgChoice> choices;
            app.GetScene().ForEachActor([&choices](engine::Actor& actor) {
                choices.push_back({actor.guid.ToString(), DescribeActor(actor)});
            });
            return choices;
        };
        command.args = {
            actorArg,
            {"path", EditorArgType::String,
             "Where to write, relative to the assets folder. Must end with .prefab, like \"prefabs/bouncer.prefab\"", true},
        };
        command.execute = [directory = std::move(assetsDirectory)](EditorCommandContext& context) {
            std::string error;
            engine::Actor* actor = FindActorFromArg(context.app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }

            // 書く先を確かめる: 拡張子、assets の外へ出ないこと、まだ無いこと(上書きは、読み込んだアセットとの食い違いを生むので、まだしない)。
            const std::string& path = context.args.Find("path")->AsString();
            if (!engine::AssetLoader::CanLoadAs(engine::PrefabAsset::StaticType(), path))
            {
                return EditorCommandResult::Failure("The path must end with .prefab: " + path);
            }
            if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
            {
                return EditorCommandResult::Failure("The path must be inside the assets folder: " + path);
            }
            const std::string fullPath = directory + "/" + path;
            const std::string metaPath = fullPath + std::string(engine::AssetDatabase::MetaSuffix);
            if (platform::FileExists(fullPath.c_str()) || platform::FileExists(metaPath.c_str()))
            {
                return EditorCommandResult::Failure("Already exists: " + path + " (choose another path)");
            }

            // アクターから値の木を作り(Prefab::Write)、JSON にしてファイルに書く。.meta には新しい GUID を書く。
            engine::Prefab::WriteResult saved = engine::Prefab::Write(*actor);
            if (!saved.ok)
            {
                EditorCommandResult failed = EditorCommandResult::Failure("Could not make a prefab from " + DescribeActor(*actor));
                failed.details = std::move(saved.warnings);
                return failed;
            }
            std::string text;
            const core::Guid guid = core::Guid::Generate();
            const std::size_t slash = fullPath.rfind('/');
            platform::MakeDirectory(fullPath.substr(0, slash).c_str()); // "prefabs" などのフォルダが無ければ作る
            if (!engine::JsonFormat::Write(saved.value, text, true) || !platform::WriteTextFile(fullPath.c_str(), text) ||
                !platform::WriteTextFile(metaPath.c_str(), engine::AssetDatabase::MakeMetaText(guid)))
            {
                return EditorCommandResult::Failure("Could not write " + fullPath);
            }

            // アセットの表を作り直し、すぐに GUID で指せるようにする(actor.spawn の prefab に渡せる)。
            context.app.GetAssetDatabase().Scan(directory);

            EditorCommandResult result =
                EditorCommandResult::Success("Created prefab " + path + " from " + DescribeActor(*actor));
            result.details.push_back("guid: " + guid.ToString());
            for (std::string& warning : saved.warnings) // アクターの外を指すハンドルを null にした、など
            {
                result.details.push_back(std::move(warning));
            }
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("guid", engine::ArchiveValue(guid.ToString()));
            result.data.Set("path", engine::ArchiveValue(path));
            return result;
        };
        return command;
    }

    // ================= ScriptableData(SD-3a) =================

    namespace
    {
        // 引数で書かれた ScriptableData(GUID の文字列か、assets の中のパス)を読み、読み終わった中身を返す。
        // 読み込み中なら待つ(エディタの操作なので、少し止まってもよい)。見つからない・読めないときは nullptr と理由。
        const engine::ScriptableData* LoadDataFromArg(engine::Application& app, const std::string& text, core::Guid& guidOut,
                                                      std::string& pathOut, std::string& error)
        {
            if (!core::Guid::Parse(text, guidOut))
            {
                const core::Guid* found = app.GetAssetDatabase().FindGuid(text);
                if (found == nullptr)
                {
                    error = "No data asset: " + text +
                            " (give its GUID or its path in the assets folder, like \"data/bouncer.sdata\")";
                    return nullptr;
                }
                guidOut = *found;
            }
            const std::string* path = app.GetAssetDatabase().FindPath(guidOut);
            if (path == nullptr || !engine::AssetLoader::CanLoadAs(engine::ScriptableData::StaticType(), *path))
            {
                error = "Not a data asset (.sdata): " + text +
                        (path != nullptr ? " (" + *path + ")" : std::string(" (not in the asset table)"));
                return nullptr;
            }
            pathOut = *path;

            engine::AssetLoader& loader = app.GetAssetLoader();
            const engine::ScriptableDataAsset* asset = loader.ResolveScriptableDataAsset(loader.LoadScriptableData(guidOut));
            if (asset != nullptr && asset->GetState() == engine::AssetState::Loading)
            {
                loader.WaitForAllLoads();
            }
            const engine::ScriptableData* data = asset != nullptr ? loader.GetScriptableData(*asset) : nullptr;
            if (data == nullptr)
            {
                error = "Could not load the data asset: " + *path + " (see log)";
            }
            return data;
        }

        // 型の登録簿にある ScriptableData の子の型(data.create の type の選択肢)。
        std::vector<EditorArgChoice> CollectDataTypeChoices(engine::Application& app, const engine::ArchiveValue&)
        {
            std::vector<EditorArgChoice> choices;
            const core::TypeRegistry& types = app.GetTypeRegistry();
            for (std::size_t i = 0; i < types.GetCount(); ++i)
            {
                const core::TypeInfo* type = types.GetType(i);
                if (type != nullptr && type->IsA(engine::ScriptableData::StaticType()))
                {
                    choices.push_back({type->GetName(), type->GetName()});
                }
            }
            return choices;
        }

        // 値の木を JSON(読みやすい形)にしてファイルに書く。書けなければ false。
        bool WriteDataFile(const engine::ArchiveValue& value, const std::string& fullPath)
        {
            std::string text;
            return engine::JsonFormat::Write(value, text, true) && platform::WriteTextFile(fullPath.c_str(), text);
        }
    }

    EditorCommand MakeCreateDataCommand(std::string assetsDirectory)
    {
        EditorCommand command;
        command.name = "data.create";
        command.label = "Create data asset";
        command.description = "Create a new data asset (.sdata, a ScriptableData) of a type, with the type's default values, "
                              "in the assets folder, and give it a GUID (.meta). Does not overwrite existing files.";
        EditorCommandArg typeArg{"type", EditorArgType::String, "ScriptableData type to create (registered by the game)", true};
        typeArg.choices = CollectDataTypeChoices;
        command.args = {
            typeArg,
            {"path", EditorArgType::String,
             "Where to write, relative to the assets folder. Must end with .sdata, like \"data/bouncer.sdata\"", true},
        };
        command.execute = [directory = std::move(assetsDirectory)](EditorCommandContext& context) {
            const std::string& typeName = context.args.Find("type")->AsString();
            const core::TypeInfo* type = context.app.GetTypeRegistry().Find(typeName);
            if (type == nullptr || !type->IsA(engine::ScriptableData::StaticType()))
            {
                return EditorCommandResult::Failure("Not a ScriptableData type: " + typeName +
                                                    (type == nullptr ? " (not registered)" : ""));
            }

            // 書く先を確かめる(prefab.create と同じ: 拡張子、assets の外へ出ないこと、まだ無いこと)。
            const std::string& path = context.args.Find("path")->AsString();
            if (!engine::AssetLoader::CanLoadAs(engine::ScriptableData::StaticType(), path))
            {
                return EditorCommandResult::Failure("The path must end with .sdata: " + path);
            }
            if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
            {
                return EditorCommandResult::Failure("The path must be inside the assets folder: " + path);
            }
            const std::string fullPath = directory + "/" + path;
            const std::string metaPath = fullPath + std::string(engine::AssetDatabase::MetaSuffix);
            if (platform::FileExists(fullPath.c_str()) || platform::FileExists(metaPath.c_str()))
            {
                return EditorCommandResult::Failure("Already exists: " + path + " (choose another path)");
            }

            // 既定の値を知るために、型から1つ作って書き出し、すぐに捨てる(フレームの終わりに破棄される)。
            core::Object* temporary = context.app.GetTypeRegistry().Create(typeName, context.app.GetRegistry());
            if (temporary == nullptr)
            {
                return EditorCommandResult::Failure("Could not create " + typeName + " (out of object memory?)");
            }
            std::vector<std::string> warnings;
            const engine::ArchiveValue value =
                engine::WriteScriptableData(*static_cast<engine::ScriptableData*>(temporary), &warnings);
            temporary->Destroy();

            const core::Guid guid = core::Guid::Generate();
            platform::MakeDirectory(fullPath.substr(0, fullPath.rfind('/')).c_str()); // "data" などのフォルダが無ければ作る
            if (!WriteDataFile(value, fullPath) ||
                !platform::WriteTextFile(metaPath.c_str(), engine::AssetDatabase::MakeMetaText(guid)))
            {
                return EditorCommandResult::Failure("Could not write " + fullPath);
            }
            context.app.GetAssetDatabase().Scan(directory); // すぐに GUID やパスで指せるようにする

            EditorCommandResult result = EditorCommandResult::Success("Created " + typeName + " data " + path);
            result.details.push_back("guid: " + guid.ToString());
            for (std::string& warning : warnings) // ハンドルのプロパティは書かない、など
            {
                result.details.push_back(std::move(warning));
            }
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("guid", engine::ArchiveValue(guid.ToString()));
            result.data.Set("path", engine::ArchiveValue(path));
            return result;
        };
        return command;
    }

    EditorCommand MakeSetDataCommand()
    {
        EditorCommand command;
        command.name = "data.set";
        command.label = "Set data value";
        command.description = "Change one value of a data asset (.sdata): writes the file and loads it again as a new asset "
                              "(loaded data is never changed in place). Things that use it switch to the new values.";
        EditorCommandArg assetArg{"asset", EditorArgType::String,
                                  "Data asset: its GUID or its path in the assets folder, like \"data/bouncer.sdata\"", true};
        assetArg.choices = [](engine::Application& app, const engine::ArchiveValue&) {
            std::vector<EditorArgChoice> choices;
            app.GetAssetDatabase().ForEachAsset([&choices](const core::Guid& guid, const std::string& path) {
                if (engine::AssetLoader::CanLoadAs(engine::ScriptableData::StaticType(), path))
                {
                    choices.push_back({guid.ToString(), path});
                }
            });
            std::sort(choices.begin(), choices.end(),
                      [](const EditorArgChoice& a, const EditorArgChoice& b) { return a.label < b.label; });
            return choices;
        };
        EditorCommandArg propertyArg{"property", EditorArgType::String,
                                     "Name of the property. Use dots to go inside a struct, like \"color.r\"", true};
        propertyArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
            std::vector<EditorArgChoice> choices;
            const engine::ArchiveValue* asset = args.Find("asset");
            core::Guid guid;
            std::string path;
            std::string ignored;
            if (asset != nullptr && asset->GetType() == engine::ArchiveValue::Type::String)
            {
                if (const engine::ScriptableData* data = LoadDataFromArg(app, asset->AsString(), guid, path, ignored))
                {
                    CollectSettablePaths(data->GetType(), std::string(), choices);
                }
            }
            return choices;
        };
        command.args = {
            assetArg,
            propertyArg,
            {"value", EditorArgType::Any,
             "New value (a number, true/false, a string, {\"name\": value} for a struct, or a guid string / null for an asset), matching the property", true},
        };

        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            core::Guid guid;
            std::string path;
            std::string error;
            const engine::ScriptableData* data = LoadDataFromArg(app, context.args.Find("asset")->AsString(), guid, path, error);
            if (data == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }

            const std::string& propertyName = context.args.Find("property")->AsString();
            const std::vector<std::string> propertyPath = SplitPropertyPath(propertyName);
            const core::Property* property = FindPropertyByPath(data->GetType(), propertyPath);
            if (property == nullptr)
            {
                return EditorCommandResult::Failure(path + " (" + data->GetType().GetName() + ") has no property '" +
                                                    propertyName + "'");
            }
            if (!IsSettableProperty(*property))
            {
                return EditorCommandResult::Failure("'" + propertyName + "' cannot be changed with data.set");
            }

            // 前の値(Undo と、人に見せるため)。
            const engine::ArchiveValue before = engine::WriteScriptableData(*data);
            const engine::ArchiveValue* found = before.Find("properties");
            for (std::size_t i = 0; i < propertyPath.size() && found != nullptr; ++i)
            {
                found = found->Find(propertyPath[i]);
            }
            if (found == nullptr)
            {
                return EditorCommandResult::Failure("Could not read the current value of " + propertyName);
            }
            const engine::ArchiveValue old = *found;

            // 読み込んだ中身は読むだけなので、書き換えない。同じ型の書き換えられる写しを作り、今の値を入れてから、
            // 新しい値を1つだけ書く(種類の確かめは、property.set と同じく ObjectSerializer::ReadProperties)。
            core::Object* copy = app.GetTypeRegistry().Create(data->GetType().GetName(), app.GetRegistry());
            if (copy == nullptr)
            {
                return EditorCommandResult::Failure("Could not create a working copy of " + path);
            }
            engine::ObjectSerializer::ReadProperties(before, *copy);
            const engine::ArchiveValue& value = *context.args.Find("value");
            const engine::ObjectSerializer::ReadReport report =
                engine::ObjectSerializer::ReadProperties(MakeNestedEntry(propertyPath, value), *copy);
            if (!report.warnings.empty())
            {
                copy->Destroy();
                return EditorCommandResult::Failure("Could not set " + propertyName + ": " + report.warnings[0]);
            }
            const engine::ArchiveValue after = engine::WriteScriptableData(*static_cast<engine::ScriptableData*>(copy));
            copy->Destroy();

            // ファイルを書き、新しいアセットとして読み直す(使っている AssetRef は、次の Resolve で新しい中身に乗り換える)。
            const std::string fullPath = app.GetAssetDatabase().GetFullPath(guid);
            if (!WriteDataFile(after, fullPath))
            {
                return EditorCommandResult::Failure("Could not write " + fullPath);
            }
            engine::AssetLoader& loader = app.GetAssetLoader();
            loader.ReloadScriptableData(guid);
            loader.WaitForAllLoads();

            std::string oldText;
            std::string newText;
            engine::JsonFormat::Write(old, oldText, false);
            engine::JsonFormat::Write(value, newText, false);
            EditorCommandResult result = EditorCommandResult::Success("Set " + path + "." + propertyName + " = " + newText);
            result.details.push_back("was " + oldText);
            result.details.push_back("guid: " + guid.ToString());

            // 戻す・やり直す: 同じ data.set を、前の値・新しい値で呼ぶ(アセットは GUID で指す)。
            auto makeCall = [&](const engine::ArchiveValue& callValue) {
                EditorCommandCall call;
                call.name = "data.set";
                call.args = engine::ArchiveValue::MakeObject();
                call.args.Set("asset", engine::ArchiveValue(guid.ToString()));
                call.args.Set("property", engine::ArchiveValue(propertyName));
                call.args.Set("value", callValue);
                return call;
            };
            result.undo = makeCall(old);
            result.redo = makeCall(value);
            return result;
        };
        return command;
    }
}

#endif
