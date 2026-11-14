// SceneCommands.h の実装。
#include "tools/SceneCommands.h"

#if HUSKY_DEVELOPMENT

#include "engine/Application.h"
#include "engine/Scene.h"
#include "platform/FileSystem.h"

#include <algorithm> // std::min
#include <string>
#include <utility> // std::move
#include <vector>

namespace husky::tools
{
    namespace
    {
        // シーンの一覧の名前を選択肢にする。
        std::vector<EditorArgChoice> SceneListChoices(engine::Application& app, const engine::ArchiveValue&)
        {
            std::vector<EditorArgChoice> choices;
            for (const engine::Application::SceneListEntry& entry : app.GetSceneList())
            {
                choices.push_back({entry.name, entry.name});
            }
            return choices;
        }

        // 読んでいるシーン(木の全部)の名前を選択肢にする。rootToo が false ならルートシーンを除く。
        std::vector<EditorArgChoice> LoadedSceneChoices(engine::Application& app, bool rootToo)
        {
            std::vector<EditorArgChoice> choices;
            std::vector<const engine::Scene*> stack;
            if (const engine::Scene* root = app.GetWorld().GetRootScene())
            {
                stack.push_back(root);
            }
            while (!stack.empty())
            {
                const engine::Scene* scene = stack.back();
                stack.pop_back();
                if ((rootToo || !scene->IsRoot()) && !scene->name.empty())
                {
                    choices.push_back({scene->name, scene->IsRoot() ? scene->name + " (root)" : scene->name});
                }
                for (const core::ObjectHandle handle : scene->children)
                {
                    if (const engine::Scene* child = app.GetRegistry().Resolve<engine::Scene>(handle))
                    {
                        stack.push_back(child);
                    }
                }
            }
            return choices;
        }

        // 名前(シーンの一覧)、GUID、assets の中のパスの順に、シーンのアセットの GUID を引く。見つからなければ null の GUID。
        core::Guid FindSceneAsset(engine::Application& app, const std::string& text)
        {
            if (const core::Guid* listed = app.FindSceneGuid(text))
            {
                return *listed;
            }
            core::Guid parsed;
            if (core::Guid::Parse(text, parsed) && app.GetAssetDatabase().FindPath(parsed) != nullptr)
            {
                return parsed;
            }
            if (const core::Guid* byPath = app.GetAssetDatabase().FindGuid(text))
            {
                return *byPath;
            }
            return core::Guid();
        }

        // そのシーンに直接いる生きているアクターの数。
        std::size_t CountActorsIn(engine::Application& app, const engine::Scene& scene)
        {
            std::size_t count = 0;
            app.GetWorld().ForEachActor([&](engine::Actor& actor) { count += actor.GetScene() == &scene ? 1 : 0; });
            return count;
        }

        EditorCommandArg SceneArg(std::string description)
        {
            EditorCommandArg arg{"scene", EditorArgType::String, std::move(description), true};
            arg.choices = SceneListChoices;
            return arg;
        }

        EditorCommandArg ChildArg(std::string description)
        {
            EditorCommandArg arg{"child", EditorArgType::String, std::move(description), true};
            arg.choices = SceneListChoices;
            return arg;
        }
    }

    EditorCommand MakeOpenSceneCommand(EditorCommandRegistry& history)
    {
        EditorCommand command;
        command.name = "scene.open";
        command.label = "Open scene";
        command.description = "Open a scene as the root scene (the world is cleared; unsaved changes are lost, and the undo history "
                              "is cleared). Child scenes marked 'load' are loaded too. F5 then saves to this scene's file.";
        command.group = "scene";
        command.args.push_back(SceneArg("Scene name in the scene list, a GUID, or a path (inside assets, or a file path)"));
        command.execute = [&history](EditorCommandContext& context) {
            engine::Application& app = context.app;
            const std::string text = context.args.Find("scene")->AsString();
            bool opened = false;
            if (app.FindSceneGuid(text) != nullptr)
            {
                opened = app.OpenRootScene(text);
            }
            else
            {
                // 一覧に無ければファイルとして開く(assets の中の相対パスか、そのままのパス)
                const std::string inAssets = app.MakeAssetPath(text);
                const std::string path = platform::FileExists(inAssets.c_str()) ? inAssets : text;
                if (!platform::FileExists(path.c_str()))
                {
                    return EditorCommandResult::Failure("No such scene in the scene list, and no such file: " + text);
                }
                opened = app.OpenRootSceneFile(path);
            }
            if (!opened)
            {
                return EditorCommandResult::Failure("Could not open the scene (see the log): " + text);
            }
            history.ClearHistory(); // 前のシーンのアクターを指す履歴は、もう使えない
            const engine::Scene* root = app.GetWorld().GetRootScene();
            EditorCommandResult result = EditorCommandResult::Success(
                "Opened scene " + root->name + " (" + std::to_string(app.GetWorld().actors.size()) + " actors, " +
                std::to_string(root->children.size()) + " child scenes loaded)");
            result.details.push_back("save to: " + app.GetRootScenePath());
            return result;
        };
        return command;
    }

    EditorCommand MakeLoadSceneCommand()
    {
        EditorCommand command;
        command.name = "scene.load";
        command.label = "Load child scene";
        command.description = "Load a scene from the scene list as a child scene (under the root scene, or under 'parent').";
        command.group = "scene";
        command.args.push_back(SceneArg("Scene name in the scene list"));
        EditorCommandArg parentArg{"parent", EditorArgType::String, "Name of a loaded scene to load under (default: the root scene)",
                                   false};
        parentArg.choices = [](engine::Application& app, const engine::ArchiveValue&) { return LoadedSceneChoices(app, true); };
        command.args.push_back(std::move(parentArg));
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            engine::World& world = app.GetWorld();
            const std::string name = context.args.Find("scene")->AsString();
            engine::Scene* parent = nullptr;
            if (const engine::ArchiveValue* parentName = context.args.Find("parent"); parentName != nullptr && !parentName->AsString().empty())
            {
                parent = world.FindScene(parentName->AsString());
                if (parent == nullptr)
                {
                    return EditorCommandResult::Failure("No such loaded scene: " + parentName->AsString());
                }
            }
            if (app.FindSceneGuid(name) == nullptr)
            {
                return EditorCommandResult::Failure("No such scene in the scene list: " + name);
            }
            if (world.FindScene(name) != nullptr)
            {
                return EditorCommandResult::Failure("The scene is already loaded (a scene can be loaded only once): " + name);
            }
            world.LoadScene(name, parent);
            app.ProcessSceneRequests(); // その場で読む(結果をすぐ返すため)
            const engine::Scene* loaded = world.FindScene(name);
            if (loaded == nullptr)
            {
                return EditorCommandResult::Failure("Could not load the scene (see the log): " + name);
            }
            return EditorCommandResult::Success("Loaded scene " + name + " (" + std::to_string(CountActorsIn(app, *loaded)) +
                                                " actors)");
        };
        return command;
    }

    EditorCommand MakeUnloadSceneCommand()
    {
        EditorCommand command;
        command.name = "scene.unload";
        command.label = "Unload child scene";
        command.description = "Unload a loaded child scene (its actors and its child scenes are destroyed). The root scene cannot be "
                              "unloaded. Unsaved changes in it are lost.";
        command.group = "scene";
        EditorCommandArg sceneArg{"scene", EditorArgType::String, "Name of a loaded child scene", true};
        sceneArg.choices = [](engine::Application& app, const engine::ArchiveValue&) { return LoadedSceneChoices(app, false); };
        command.args.push_back(std::move(sceneArg));
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            const std::string name = context.args.Find("scene")->AsString();
            engine::Scene* scene = app.GetWorld().FindScene(name);
            if (scene == nullptr)
            {
                return EditorCommandResult::Failure("No such loaded scene: " + name);
            }
            if (scene->IsRoot())
            {
                return EditorCommandResult::Failure("The root scene cannot be unloaded (open another scene instead): " + name);
            }
            std::size_t destroyed = 0;
            app.GetWorld().UnloadSceneNow(*scene, &destroyed);
            return EditorCommandResult::Success("Unloaded scene " + name + " (" + std::to_string(destroyed) + " actors)");
        };
        return command;
    }

    EditorCommand MakeAddChildSceneCommand()
    {
        EditorCommand command;
        command.name = "scene.addChild";
        command.label = "Add child scene";
        command.description = "Add a child scene record to the root scene (saved in the root scene's file). With 'load', the child "
                              "is loaded whenever the root scene is opened. Does not load it now (use scene.load).";
        command.group = "scene";
        command.modifiesScene = true;
        command.args.push_back(ChildArg("Child scene: name in the scene list, a GUID, or a path inside assets"));
        EditorCommandArg loadArg{"load", EditorArgType::Bool, "Load it when the root scene is opened (default: true)", false};
        loadArg.defaultValue = engine::ArchiveValue(true);
        command.args.push_back(std::move(loadArg));
        command.args.push_back(EditorCommandArg{"index", EditorArgType::Int, "Position in the records (default: at the end)", false});
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            engine::Scene* root = app.GetWorld().GetRootScene();
            const std::string text = context.args.Find("child")->AsString();
            const core::Guid guid = FindSceneAsset(app, text);
            if (root == nullptr || guid.IsNull())
            {
                return EditorCommandResult::Failure("No such scene (not in the scene list, not a known GUID or path): " + text);
            }
            if (guid == root->asset)
            {
                return EditorCommandResult::Failure("A scene cannot contain itself: " + text);
            }
            for (const engine::Scene::ChildSceneEntry& entry : root->childScenes)
            {
                if (entry.scene == guid)
                {
                    return EditorCommandResult::Failure("The root scene already has this child scene: " + text);
                }
            }
            engine::Scene::ChildSceneEntry entry;
            entry.scene = guid;
            const engine::ArchiveValue* load = context.args.Find("load");
            entry.loadOnOpen = load == nullptr || load->AsBool(true);
            std::size_t index = root->childScenes.size();
            if (const engine::ArchiveValue* at = context.args.Find("index"); at != nullptr && at->AsInt(-1) >= 0)
            {
                index = std::min(static_cast<std::size_t>(at->AsInt()), root->childScenes.size());
            }
            root->childScenes.insert(root->childScenes.begin() + static_cast<std::ptrdiff_t>(index), entry);

            const std::string* path = app.GetAssetDatabase().FindPath(guid);
            EditorCommandResult result = EditorCommandResult::Success(
                "Added child scene " + (path != nullptr ? *path : guid.ToString()) + (entry.loadOnOpen ? " (load on open)" : ""));
            engine::ArchiveValue undoArgs = engine::ArchiveValue::MakeObject();
            undoArgs.Set("child", engine::ArchiveValue(guid.ToString()));
            result.undo = {"scene.removeChild", undoArgs};
            engine::ArchiveValue redoArgs = undoArgs;
            redoArgs.Set("load", engine::ArchiveValue(entry.loadOnOpen));
            redoArgs.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(index)));
            result.redo = {"scene.addChild", redoArgs};
            return result;
        };
        return command;
    }

    EditorCommand MakeRemoveChildSceneCommand()
    {
        EditorCommand command;
        command.name = "scene.removeChild";
        command.label = "Remove child scene";
        command.description = "Remove a child scene record from the root scene. Does not unload it now (use scene.unload).";
        command.group = "scene";
        command.modifiesScene = true;
        command.args.push_back(ChildArg("Child scene: name in the scene list, a GUID, or a path inside assets"));
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            engine::Scene* root = app.GetWorld().GetRootScene();
            const std::string text = context.args.Find("child")->AsString();
            const core::Guid guid = FindSceneAsset(app, text);
            if (root == nullptr || guid.IsNull())
            {
                return EditorCommandResult::Failure("No such scene: " + text);
            }
            for (std::size_t i = 0; i < root->childScenes.size(); ++i)
            {
                if (root->childScenes[i].scene != guid)
                {
                    continue;
                }
                const bool load = root->childScenes[i].loadOnOpen;
                root->childScenes.erase(root->childScenes.begin() + static_cast<std::ptrdiff_t>(i));
                EditorCommandResult result = EditorCommandResult::Success("Removed child scene " + text);
                engine::ArchiveValue undoArgs = engine::ArchiveValue::MakeObject();
                undoArgs.Set("child", engine::ArchiveValue(guid.ToString()));
                undoArgs.Set("load", engine::ArchiveValue(load));
                undoArgs.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(i)));
                result.undo = {"scene.addChild", undoArgs};
                engine::ArchiveValue redoArgs = engine::ArchiveValue::MakeObject();
                redoArgs.Set("child", engine::ArchiveValue(guid.ToString()));
                result.redo = {"scene.removeChild", redoArgs};
                return result;
            }
            return EditorCommandResult::Failure("The root scene has no such child scene record: " + text);
        };
        return command;
    }

    EditorCommand MakeListScenesCommand()
    {
        EditorCommand command;
        command.name = "scene.list";
        command.label = "List scenes";
        command.description = "Show the tree of loaded scenes, their actor counts, and the root scene's child scene records.";
        command.group = "scene";
        command.readsScene = true;
        command.execute = [](EditorCommandContext& context) {
            engine::Application& app = context.app;
            EditorCommandResult result = EditorCommandResult::Success("");
            result.data = engine::ArchiveValue::MakeArray();
            struct Pending
            {
                const engine::Scene* scene;
                int depth;
            };
            std::vector<Pending> stack;
            if (const engine::Scene* root = app.GetWorld().GetRootScene())
            {
                stack.push_back({root, 0});
            }
            std::size_t sceneCount = 0;
            while (!stack.empty())
            {
                const Pending current = stack.back();
                stack.pop_back();
                ++sceneCount;
                const std::size_t actors = CountActorsIn(app, *current.scene);
                const std::string name = current.scene->name.empty() ? std::string("(unnamed)") : current.scene->name;
                result.details.push_back(std::string(static_cast<std::size_t>(current.depth) * 2, ' ') + name +
                                         (current.scene->IsRoot() ? " (root)" : "") + ": " + std::to_string(actors) + " actors");
                engine::ArchiveValue item = engine::ArchiveValue::MakeObject();
                item.Set("name", engine::ArchiveValue(current.scene->name));
                item.Set("asset", engine::ArchiveValue(current.scene->asset.ToString()));
                item.Set("depth", engine::ArchiveValue(static_cast<std::int64_t>(current.depth)));
                item.Set("actors", engine::ArchiveValue(static_cast<std::int64_t>(actors)));
                result.data.Append(std::move(item));
                for (auto it = current.scene->children.rbegin(); it != current.scene->children.rend(); ++it)
                {
                    if (const engine::Scene* child = app.GetRegistry().Resolve<engine::Scene>(*it); child != nullptr && !child->IsPendingDestroy())
                    {
                        stack.push_back({child, current.depth + 1});
                    }
                }
            }
            if (const engine::Scene* root = app.GetWorld().GetRootScene())
            {
                for (const engine::Scene::ChildSceneEntry& entry : root->childScenes)
                {
                    const std::string* path = app.GetAssetDatabase().FindPath(entry.scene);
                    result.details.push_back("record: " + (path != nullptr ? *path : entry.scene.ToString()) +
                                             (entry.loadOnOpen ? " (load on open)" : ""));
                }
            }
            result.message = std::to_string(sceneCount) + " scene(s) loaded";
            return result;
        };
        return command;
    }
}

#endif
