// EditorCommands.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCommands.h"

#if HUSKY_DEVELOPMENT

#include "core/TypeRegistry.h"      // actor.spawn: 型の名前から作る
#include "engine/Actor.h"
#include "engine/JsonFormat.h"       // property.set: 値を人に見せる
#include "engine/ObjectSerializer.h" // property.set: 値の確かめと書き込み
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
            // どの種類でもよい(null だけは「無い」と同じなので断る)。中身はコマンドが確かめる。
            case EditorArgType::Any: matches = type != Type::Null; break;
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
                return true;
            default:
                return false; // ハンドル、配列、構造体、アセットの参照、guid は、今は書き換えない
            }
        }
    }

    std::string DescribeActor(const engine::Actor& actor)
    {
        const char* typeName = actor.GetType().GetName();
        if (actor.name.empty())
        {
            return std::string(typeName) + " at " + FormatPosition(actor.x, actor.y);
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
                     FormatPosition(actor.x, actor.y);
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
                item.Set("x", engine::ArchiveValue(actor.x));
                item.Set("y", engine::ArchiveValue(actor.y));
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

    EditorCommand MakeSetPropertyCommand()
    {
        EditorCommand command;
        command.name = "property.set";
        command.label = "Set property";
        command.description = "Change one property of an actor, like its position or speed. "
                              "The actor can be given by name (if unique) or by guid.";
        command.modifiesScene = true;

        EditorCommandArg actorArg{"actor", EditorArgType::String, "Name (if unique) or guid of the actor", true};
        actorArg.choices = ActorChoices;
        // 選べるプロパティは、選んだアクターの型で決まる(ほかの引数 actor を見て一覧を作る)。
        EditorCommandArg propertyArg{"property", EditorArgType::String, "Name of the property, like \"x\"", true};
        propertyArg.choices = [](engine::Application& app, const engine::ArchiveValue& args) {
            std::vector<EditorArgChoice> choices;
            const engine::ArchiveValue* actorValue = args.Find("actor");
            std::string ignored;
            const engine::Actor* actor =
                actorValue != nullptr ? FindActorFromArg(app.GetScene(), actorValue->AsString(), ignored) : nullptr;
            if (actor != nullptr)
            {
                actor->GetType().ForEachProperty([&choices](const core::Property& property) {
                    if (IsSettableProperty(property))
                    {
                        choices.push_back({property.GetName(), std::string()});
                    }
                });
            }
            return choices;
        };
        command.args = {
            actorArg,
            propertyArg,
            {"value", EditorArgType::Any, "New value (a number, true/false, or a string, matching the property)", true},
        };

        command.execute = [](EditorCommandContext& context) {
            std::string error;
            engine::Actor* actor = FindActorFromArg(context.app.GetScene(), context.args.Find("actor")->AsString(), error);
            if (actor == nullptr)
            {
                return EditorCommandResult::Failure(error);
            }
            const std::string& propertyName = context.args.Find("property")->AsString();
            const core::Property* property = actor->GetType().FindProperty(propertyName.c_str());
            if (property == nullptr)
            {
                return EditorCommandResult::Failure(DescribeActor(*actor) + " has no property '" + propertyName + "'");
            }
            if (!IsSettableProperty(*property))
            {
                return EditorCommandResult::Failure("'" + propertyName + "' cannot be changed with property.set");
            }

            // 人に見せるアクターの説明は、書き換える前のもの(名前や位置を変えたときに、どのアクターだったか分かるように)。
            const std::string who = DescribeActor(*actor);
            // 前の値(人に見せるため)。保存と同じ道具で、このアクターのプロパティを値の木にして、その名前のものを取り出す。
            const engine::ArchiveValue before = engine::ObjectSerializer::Write(*actor);
            std::string oldText;
            if (const engine::ArchiveValue* oldProperties = before.Find("properties"))
            {
                if (const engine::ArchiveValue* old = oldProperties->Find(propertyName))
                {
                    engine::JsonFormat::Write(*old, oldText, false);
                }
            }

            // 新しい値を書く: シーンの読み込みと同じ道具(ObjectSerializer::ReadProperties)を通すので、
            // 種類の確かめ(「数ではありません」など)も、ファイルを読むときとそろう。
            engine::ArchiveValue entry = engine::ArchiveValue::MakeObject();
            engine::ArchiveValue& properties = entry.Set("properties", engine::ArchiveValue::MakeObject());
            properties.Set(propertyName, *context.args.Find("value"));
            const engine::ObjectSerializer::ReadReport report = engine::ObjectSerializer::ReadProperties(entry, *actor);
            if (!report.warnings.empty())
            {
                return EditorCommandResult::Failure("Could not set " + propertyName + ": " + report.warnings[0]);
            }

            std::string newText;
            engine::JsonFormat::Write(*context.args.Find("value"), newText, false);
            EditorCommandResult result =
                EditorCommandResult::Success("Set " + who + "." + propertyName + " = " + newText);
            result.details.push_back("was " + oldText);
            result.details.push_back("guid: " + actor->guid.ToString());
            return result;
        };
        return command;
    }
}

#endif
