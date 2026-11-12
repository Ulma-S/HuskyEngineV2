// EditorCommands.h の実装。製品(Release)では中身が空になる。
#include "tools/EditorCommands.h"

#if HUSKY_DEVELOPMENT

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

    EditorCommandResult EditorCommandRegistry::Execute(const std::string& name, engine::Application& app,
                                                       const engine::ArchiveValue& args)
    {
        EditorCommandResult result;
        if (const EditorCommand* command = Find(name))
        {
            EditorCommandContext context{app, args};
            result = command->execute(context);
        }
        else
        {
            result = EditorCommandResult::Failure("Unknown command: " + name);
        }

        // 人が後から追えるように、ログにも出す(画面の窓を閉じていても分かる)。
        std::printf("コマンド %s: %s %s\n", name.c_str(), result.succeeded ? "成功" : "失敗", result.message.c_str());
        for (const std::string& detail : result.details)
        {
            std::printf("  %s\n", detail.c_str());
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
}

#endif
