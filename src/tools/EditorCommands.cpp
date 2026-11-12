// EditorCommands.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCommands.h"

#if HUSKY_DEVELOPMENT

#include "core/TypeRegistry.h"      // actor.spawn: 型の名前から作る
#include "engine/Actor.h"
#include "engine/AssetDatabase.h"   // CreateMissingMetaFiles、Scan
#include "engine/SpriteSheetData.h" // NormalizeSpriteSheetsInFolder

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
        for (std::size_t i = 0; i < m_commands.size(); ++i)
        {
            const engine::Key shortcut = m_commands[i].shortcut;
            if (shortcut != engine::Key::Unknown && app.GetInput().WasPressed(shortcut))
            {
                const std::string name = m_commands[i].name;
                Execute(name, app);
                ++executed;
            }
        }
        return executed;
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

    EditorCommand MakeSpawnActorCommand()
    {
        EditorCommand command;
        command.name = "actor.spawn";
        command.label = "Spawn actor";
        command.description = "Place a new actor of the given type in the scene. Only the actor is created "
                              "(no components such as a sprite).";
        command.modifiesScene = true;
        EditorCommandArg typeArg{"type", EditorArgType::String,
                                 "Type name of the actor, like \"BouncerActor\" (must derive from Actor)", true};
        // 選べるのは、型の登録簿にある Actor の子孫(エンジンの型と、ゲームが登録した型)。
        typeArg.choices = [](engine::Application& app) {
            std::vector<std::string> names;
            const core::TypeRegistry& types = app.GetTypeRegistry();
            for (std::size_t i = 0; i < types.GetCount(); ++i)
            {
                const core::TypeInfo* type = types.GetType(i);
                if (type != nullptr && type->IsA(engine::Actor::StaticType()))
                {
                    names.emplace_back(type->GetName());
                }
            }
            return names;
        };
        command.args = {
            typeArg,
            {"x", EditorArgType::Float, "X position in pixels", false, engine::ArchiveValue(0.0)},
            {"y", EditorArgType::Float, "Y position in pixels", false, engine::ArchiveValue(0.0)},
        };
        command.execute = [](EditorCommandContext& context) {
            // 引数は Execute が形で確かめ済みなので、ここでは種類の心配をせずに読める。
            const std::string& typeName = context.args.Find("type")->AsString();
            const engine::ArchiveValue* xValue = context.args.Find("x");
            const engine::ArchiveValue* yValue = context.args.Find("y");
            const float x = xValue != nullptr ? static_cast<float>(xValue->AsFloat()) : 0.0f;
            const float y = yValue != nullptr ? static_cast<float>(yValue->AsFloat()) : 0.0f;

            // 型の名前から型を探し、アクターの子孫かを確かめてから作る(シーンのファイルを読むときと同じ道具を使う)。
            const core::TypeRegistry& types = context.app.GetTypeRegistry();
            const core::TypeInfo* type = types.Find(typeName);
            if (type == nullptr)
            {
                return EditorCommandResult::Failure("Unknown type: " + typeName);
            }
            if (!type->IsA(engine::Actor::StaticType()))
            {
                return EditorCommandResult::Failure(typeName + " is not an actor");
            }
            engine::Scene& scene = context.app.GetScene();
            engine::Actor* actor = core::Cast<engine::Actor>(types.Create(typeName, context.app.GetRegistry()));
            if (actor == nullptr || !scene.AddLoadedActor(*actor))
            {
                return EditorCommandResult::Failure("Could not create " + typeName + " (out of object memory?)");
            }
            actor->x = x;
            actor->y = y;
            // 位置は %g で短く書く(std::to_string は 100.000000 のように桁を並べて読みにくい)。
            char position[64];
            std::snprintf(position, sizeof(position), " at (%g, %g)", static_cast<double>(x), static_cast<double>(y));
            return EditorCommandResult::Success("Spawned " + typeName + position);
        };
        return command;
    }
}

#endif
