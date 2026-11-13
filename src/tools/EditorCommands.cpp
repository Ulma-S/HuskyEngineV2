// EditorCommands.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCommands.h"

#if HUSKY_DEVELOPMENT

#include "core/TypeRegistry.h"      // actor.spawn: 型の名前から作る
#include "engine/Actor.h"
#include "engine/AssetLoader.h"     // actor.spawn: プレハブを読む
#include "engine/Prefab.h"          // actor.spawn、prefab.create
#include "platform/FileSystem.h"    // prefab.create: ファイルを書く
#include "engine/JsonFormat.h"       // property.set: 値を人に見せる
#include "engine/ObjectSerializer.h" // property.set: 値の確かめと書き込み
#include "engine/SceneSerializer.h"  // actor.destroy / actor.restore: アクター1体を控える・作り直す
#include "engine/AssetDatabase.h"   // CreateMissingMetaFiles、Scan
#include "engine/SpriteSheetData.h" // NormalizeSpriteSheetsInFolder

#include <algorithm> // std::sort
#include <cstdio>  // std::printf
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
            std::printf("コマンド %s: %s %s\n", name.c_str(), result.succeeded ? "成功" : "失敗",
                        result.message.c_str());
            for (const std::string& detail : result.details)
            {
                std::printf("  %s\n", detail.c_str());
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
        EditorCommandCall MakeRestoreCall(const engine::ArchiveValue& data);
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
            result.redo = MakeRestoreCall(engine::SceneSerializer::WriteActors(scene, {actor}).value);
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
            case core::PropertyType::String:
            case core::PropertyType::Struct: // 構造体は {"x": 1} のような形で、書いた値だけを書き換える
            case core::PropertyType::AssetRef: // GUID の文字列か、null(何も指さない)
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

            // 値1つを、ファイルと同じ入れ子の形にする。構造体の読み込みは書いてある値だけを書き換えるので、
            //   "transform.position.x" → {"properties": {"transform": {"position": {"x": 値}}}}(ほかの値はそのまま)
            auto makeEntry = [&path](const engine::ArchiveValue& entryValue) {
                engine::ArchiveValue entry = engine::ArchiveValue::MakeObject();
                engine::ArchiveValue* parent = &entry.Set("properties", engine::ArchiveValue::MakeObject());
                for (std::size_t i = 0; i + 1 < path.size(); ++i)
                {
                    parent = &parent->Set(path[i], engine::ArchiveValue::MakeObject());
                }
                parent->Set(path.back(), entryValue);
                return entry;
            };

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
                item.Set("actor", engine::ArchiveValue(guid));
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
                        engine::ArchiveValue back = *it;
                        back.Set("actor", engine::ArchiveValue(guid));
                        std::string ignored;
                        SetOneProperty(app, back, found, old, who, ignored);
                    }
                    return false;
                }
                const engine::ArchiveValue* component = values.At(i).Find("component");
                undone.push_back(MakeSetItem(std::string(), component != nullptr ? component->AsString() : std::string(),
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
             "the same as the arguments of property.set", true},
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

        // actor.restore の呼び出し(actor.destroy の Undo、actor.restore の Redo 用)。data は控えたアクター1体。
        EditorCommandCall MakeRestoreCall(const engine::ArchiveValue& data)
        {
            EditorCommandCall call;
            call.name = "actor.restore";
            call.args = engine::ArchiveValue::MakeObject();
            call.args.Set("data", data);
            return call;
        }

        // 控え(シーンのファイルと同じ形、アクター1体)から、そのアクターの guid を読む。
        // 形: {"objects": [{"id": 0, "properties": {"guid": "..."}}, ...], "actors": [0]}。読めなければ false。
        bool ReadSavedActorGuid(const engine::ArchiveValue& data, core::Guid& out)
        {
            const engine::ArchiveValue* actors = data.Find("actors");
            const engine::ArchiveValue* objects = data.Find("objects");
            if (actors == nullptr || objects == nullptr || actors->GetType() != engine::ArchiveValue::Type::Array ||
                objects->GetType() != engine::ArchiveValue::Type::Array || actors->GetArraySize() != 1)
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
        command.description = "Remove an actor (and its components) from the scene. "
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
            // 消す前に、シーンのファイルと同じ形で控える(guid、プレハブとの違い、コンポーネントが入る)。
            // Undo はこの控えを actor.restore に渡し、同じ guid のアクターを作り直す。
            engine::SceneSerializer::WriteResult saved = engine::SceneSerializer::WriteActors(scene, {actor});
            const std::string who = DescribeActor(*actor);
            const core::Guid guid = actor->guid;
            actor->Destroy(); // フレームの終わりに一覧から外れる(コンポーネントもそのときに消える)
            EditorCommandResult result = EditorCommandResult::Success("Destroyed " + who);
            // 外を指すハンドル(カメラの追う相手など)は控えに入らない。戻しても null になることを人に知らせる。
            result.details = std::move(saved.warnings);
            result.undo = MakeRestoreCall(saved.value);
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
        command.description = "Put back an actor saved by actor.destroy (used by editor.undo), with the same guid. "
                              "The actor is added at the end of the scene list.";
        command.modifiesScene = true;
        command.args = {
            {"data", EditorArgType::Any,
             "One actor in the same form as a scene file ({\"objects\": [...], \"actors\": [id]}), with its guid", true},
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
            if (report.addedActorCount != 1 || actor == nullptr)
            {
                EditorCommandResult failed = EditorCommandResult::Failure("Could not restore the actor");
                failed.details = std::move(report.warnings);
                return failed;
            }
            EditorCommandResult result = EditorCommandResult::Success("Restored " + DescribeActor(*actor));
            result.details = std::move(report.warnings);
            result.undo = MakeDestroyCall(guid);
            result.redo = MakeRestoreCall(data);
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("guid", engine::ArchiveValue(guid.ToString()));
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
            // 型の名前から型を探し、コンポーネントの子孫かを確かめてから作る(actor.spawn の type と同じ流れ)。
            const std::string& typeName = context.args.Find("type")->AsString();
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
            const engine::ArchiveValue* indexValue = context.args.Find("index");
            if (indexValue != nullptr && indexValue->AsInt(-1) < 0)
            {
                return EditorCommandResult::Failure("\"index\" must be 0 or more");
            }
            engine::Component* component = core::Cast<engine::Component>(types.Create(typeName, app.GetRegistry()));
            if (component == nullptr)
            {
                return EditorCommandResult::Failure("Could not create " + typeName + " (out of object memory?)");
            }
            std::vector<std::string> warnings;
            if (const engine::ArchiveValue* data = context.args.Find("data"))
            {
                // 控えから戻す(Undo): 値も prefabComponentId も控えのとおり。プレハブとの組が元どおりになる。
                LoadComponent(app, *data, *component, warnings);
            }
            const std::size_t wanted = indexValue != nullptr ? static_cast<std::size_t>(indexValue->AsInt()) : actor->components.size();
            const std::size_t index = actor->InsertComponent(*component, wanted);

            EditorCommandResult result = EditorCommandResult::Success("Added " + typeName + " to " + DescribeActor(*actor));
            result.details = std::move(warnings);
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(index)));
            // Undo は、入れた位置のものを外す。Redo は、足した直後の控えで同じ位置に足し直す(値と ID がそろう)。
            std::vector<std::string> ignored; // 足した直後の控えの警告は、上の details と同じことしか言わない
            result.undo = MakeRemoveComponentCall(actor->guid, typeName, index);
            result.redo = MakeAddComponentCall(actor->guid, typeName, index, SaveComponent(app, *component, ignored));
            return result;
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
            return actor != nullptr ? engine::Prefab::FindOverrides(*actor) : engine::Prefab::OverridesResult{};
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
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor);
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
            for (const std::string& type : found.addedComponents)
            {
                result.details.push_back("+ " + type + " (added, not in the prefab)");
            }
            for (const std::string& type : found.removedComponents)
            {
                result.details.push_back("- " + type + " (removed, still in the prefab)");
            }
            return result;
        };
        return command;
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
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor);
            if (found.prefab == nullptr)
            {
                return EditorCommandResult::Failure(who + ": " + found.error);
            }
            const std::vector<const engine::Prefab::Override*> selected = SelectOverrides(found, context.args);
            const engine::ArchiveValue* propertyValue = context.args.Find("property");
            if (selected.empty())
            {
                if (propertyValue != nullptr)
                {
                    return EditorCommandResult::Failure(who + " has no override at '" + propertyValue->AsString() + "'");
                }
                return EditorCommandResult::Success("Nothing to revert on " + who); // 戻すものが無い(Undo の履歴にも積まない)
            }

            // プレハブの値を書く。property.setMany と同じ道具なので、Undo も property.setMany(前の値)になる。
            engine::ArchiveValue values = engine::ArchiveValue::MakeArray();
            for (const engine::Prefab::Override* item : selected)
            {
                values.Append(MakeSetItem(std::string(), item->component, item->path, item->prefabValue));
            }
            engine::ArchiveValue olds;
            if (!SetManyProperties(context.app, *actor, values, olds, error))
            {
                return EditorCommandResult::Failure("Could not revert " + who + ": " + error);
            }
            EditorCommandResult result =
                EditorCommandResult::Success("Reverted " + std::to_string(selected.size()) + " value(s) of " + who);
            for (const engine::Prefab::Override* item : selected)
            {
                result.details.push_back(DescribeOverride(*item));
            }
            result.undo = MakeSetManyCall(*actor, olds);
            result.redo = MakeSetManyCall(*actor, values);
            return result;
        };
        return command;
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
            const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(*actor);
            if (found.prefab == nullptr)
            {
                return EditorCommandResult::Failure(who + ": " + found.error);
            }
            const std::vector<const engine::Prefab::Override*> selected = SelectOverrides(found, context.args);
            const engine::ArchiveValue* propertyValue = context.args.Find("property");
            if (selected.empty())
            {
                if (propertyValue != nullptr)
                {
                    return EditorCommandResult::Failure(who + " has no override at '" + propertyValue->AsString() + "'");
                }
                return EditorCommandResult::Success("Nothing to apply from " + who);
            }
            const core::Guid guid = found.prefab->GetGuid();
            const std::string* relativePath = app.GetAssetDatabase().FindPath(guid);
            if (relativePath == nullptr)
            {
                return EditorCommandResult::Failure("The prefab " + guid.ToString() + " is not in the asset table");
            }
            const std::string prefabPath = *relativePath; // 表を作り直しても消えないように写す

            // 1. 新しいプレハブの値を作り、ファイルに書く(読み込んだアセットは書き換えない。変わらないデータなので)
            std::vector<engine::Prefab::ValueChange> changes;
            for (const engine::Prefab::Override* item : selected)
            {
                changes.push_back({item->component, item->path, item->prefabValue, item->value, item->componentId});
            }
            engine::ArchiveValue newData;
            std::string text;
            if (!engine::Prefab::WriteChanges(found.prefab->GetData(), changes, newData, error))
            {
                return EditorCommandResult::Failure("Could not apply to " + prefabPath + ": " + error);
            }
            const std::string fullPath = directory + "/" + prefabPath;
            if (!engine::JsonFormat::Write(newData, text, true) || !platform::WriteTextFile(fullPath.c_str(), text))
            {
                return EditorCommandResult::Failure("Could not write " + fullPath);
            }

            // 2. ファイルから読み直す(新しいアセットになる)
            engine::AssetLoader& loader = app.GetAssetLoader();
            const core::ObjectHandle reloaded = loader.ReloadPrefab(guid);
            loader.WaitForAllLoads();
            const engine::PrefabAsset* newPrefab = loader.ResolvePrefab(reloaded);
            if (newPrefab == nullptr || !newPrefab->IsLoaded())
            {
                return EditorCommandResult::Failure("Wrote " + prefabPath + " but could not load it again");
            }

            // 3. 同じプレハブから置いた全員に伝える: 古いプレハブの値のままのものだけが、新しい値になる。
            //    全員が新しいアセットを指すようにする(次の保存では、新しいプレハブと比べた違いになる)。
            std::size_t followed = 0;
            app.GetScene().ForEachActor([&](engine::Actor& other) {
                if (other.prefab.guid == guid)
                {
                    if (engine::Prefab::FollowChanges(other, changes) > 0)
                    {
                        ++followed;
                    }
                    other.prefab.asset = reloaded;
                }
            });

            EditorCommandResult result = EditorCommandResult::Success(
                "Applied " + std::to_string(selected.size()) + " value(s) of " + who + " to " + prefabPath + "; " +
                std::to_string(followed) + " other actor(s) followed");
            for (const engine::Prefab::Override* item : selected)
            {
                result.details.push_back(DescribeOverride(*item));
            }
            result.data = engine::ArchiveValue::MakeObject();
            result.data.Set("prefab", engine::ArchiveValue(prefabPath));
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
}

#endif
