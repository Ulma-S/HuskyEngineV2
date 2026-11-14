// GameModule.h の実装。
#include "engine/GameModule.h"

#include "core/ObjectRegistry.h"
#include "core/TypeRegistry.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/Prefab.h"
#include "engine/SceneSerializer.h"
#include "platform/DynamicLibrary.h"
#include "platform/FileSystem.h"

namespace husky::engine
{
    bool LoadGameModule(platform::DynamicLibrary& library, const std::string& path, GameModule& out)
    {
        out = GameModule{};
        if (!library.Load(path.c_str()))
        {
            return false; // 理由は DynamicLibrary がログに出す
        }
        // 探した関数は「型の分からないポインタ」なので、約束の型(GameModuleEntryFunction)に変えてから呼ぶ。
        auto entry = reinterpret_cast<GameModuleEntryFunction>(library.FindFunction(GameModuleEntryName));
        if (entry == nullptr)
        {
            library.Unload();
            return false;
        }
        GameModule module;
        if (!entry(&module))
        {
            HUSKY_LOG(ModuleLog::EntryFailed, path);
            library.Unload();
            return false;
        }
        if (module.version != GameModuleVersion)
        {
            HUSKY_LOG(ModuleLog::VersionMismatch, path, module.version, GameModuleVersion);
            library.Unload();
            return false;
        }
        out = module;
        HUSKY_LOG(ModuleLog::Loaded, path);
        return true;
    }

    GameModuleHost::~GameModuleHost()
    {
        const std::string loaded = m_library.GetPath();
        m_library.Unload(); // ここで外す(写しのファイルを消せるように)
        if (!loaded.empty())
        {
            platform::RemoveFile(loaded.c_str());
        }
        if (!m_previousCopy.empty())
        {
            platform::RemoveFile(m_previousCopy.c_str());
        }
    }

    bool GameModuleHost::LoadFromLibrary(const std::string& sourcePath, core::TypeRegistry& types)
    {
        // 写しの名前: HuskyGame.dll → HuskyGame.loaded1.dll(読むたびに番号を増やす)
        const std::string suffix = platform::DynamicLibrary::GetFileSuffix();
        std::string stem = sourcePath;
        if (stem.size() >= suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            stem.resize(stem.size() - suffix.size());
        }
        const std::string copyPath = stem + ".loaded" + std::to_string(++m_loadCount) + suffix;
        if (!platform::DuplicateFile(sourcePath.c_str(), copyPath.c_str()))
        {
            return false;
        }
        if (!LoadCopy(copyPath, types))
        {
            platform::RemoveFile(copyPath.c_str());
            return false;
        }
        m_sourcePath = sourcePath;
        if (!m_previousCopy.empty())
        {
            platform::RemoveFile(m_previousCopy.c_str()); // 新しいものを読めたので、戻す先は要らない
            m_previousCopy.clear();
        }
        return true;
    }

    bool GameModuleHost::RestorePreviousLibrary(core::TypeRegistry& types)
    {
        if (m_previousCopy.empty() || m_attached)
        {
            return false;
        }
        const std::string copyPath = m_previousCopy;
        m_previousCopy.clear();
        return LoadCopy(copyPath, types);
    }

    bool GameModuleHost::LoadCopy(const std::string& copyPath, core::TypeRegistry& types)
    {
        GameModule module;
        if (!LoadGameModule(m_library, copyPath, module))
        {
            return false;
        }
        m_module = module;
        RegisterTypes(types);
        return true;
    }

    void GameModuleHost::Attach(const GameModule& module, core::TypeRegistry& types)
    {
        m_module = module;
        RegisterTypes(types);
    }

    void GameModuleHost::RegisterTypes(core::TypeRegistry& types)
    {
        // 登録簿は末尾に足していくので、呼ぶ前の数から後ろが、ゲームが登録した型。
        const std::size_t before = types.GetCount();
        if (m_module.registerTypes != nullptr)
        {
            m_module.registerTypes(types);
        }
        m_types.clear();
        for (std::size_t i = before; i < types.GetCount(); ++i)
        {
            m_types.push_back(types.GetType(i));
        }
        m_attached = true;
    }

    bool GameModuleHost::IsGameType(const core::TypeInfo& type) const
    {
        for (const core::TypeInfo* gameType : m_types)
        {
            if (type.IsA(*gameType))
            {
                return true;
            }
        }
        return false;
    }

    std::size_t GameModuleHost::CountGameObjects(core::ObjectRegistry& registry, const core::TypeInfo** firstType) const
    {
        std::size_t count = 0;
        registry.ForEachObject([&](core::Object& object) {
            if (IsGameType(object.GetType()))
            {
                if (count == 0 && firstType != nullptr)
                {
                    *firstType = &object.GetType();
                }
                ++count;
            }
        });
        return count;
    }

    bool GameModuleHost::Detach(core::TypeRegistry& types, core::ObjectRegistry& registry, AssetLoader& loader)
    {
        if (!m_attached)
        {
            return true;
        }
        ForgetScriptableData(loader, m_types);
        Prefab::ClearDefaultPropertiesCache();
        registry.FlushDestroyed();
        registry.CollectGarbage();

        const core::TypeInfo* firstType = nullptr;
        const std::size_t remaining = CountGameObjects(registry, &firstType);
        if (remaining > 0)
        {
            HUSKY_LOG(ModuleLog::ObjectsRemain, static_cast<int>(remaining), firstType != nullptr ? firstType->GetName() : "");
            return false;
        }

        for (const core::TypeInfo* type : m_types)
        {
            types.Unregister(*type);
        }
        const std::string path = m_library.GetPath();
        m_types.clear();
        m_module = GameModule{};
        m_attached = false;
        if (m_library.IsLoaded())
        {
            m_library.Unload();
            m_previousCopy = path; // 写しのファイルは、次を読めるまで消さない(読めなければ戻すため)
            HUSKY_LOG(ModuleLog::Unloaded, path);
        }
        return true;
    }

    GameModuleReloadResult ReloadGameModule(Application& app)
    {
        GameModuleReloadResult result;
        GameModuleHost& host = app.GetGameModule();
        if (!host.IsFromLibrary())
        {
            result.message = "The game code is not loaded from a library (static link)";
            return result;
        }
        Scene& scene = app.GetScene();
        core::ObjectRegistry& registry = app.GetRegistry();
        core::TypeRegistry& types = app.GetTypeRegistry();
        const std::string sourcePath = host.GetSourcePath();

        // 1. シーンを控える(Play と同じ形)
        SceneSerializer::WriteResult saved = SceneSerializer::Write(scene);
        result.warnings = std::move(saved.warnings);
        const std::size_t actorCount = scene.actors.size();
        const auto restoreScene = [&]() {
            const SceneSerializer::ReadReport report = SceneSerializer::Read(
                saved.value, scene, types, [&app](const core::Guid& guid) { return app.FindPrefabForScene(guid); });
            result.warnings.insert(result.warnings.end(), report.warnings.begin(), report.warnings.end());
        };

        // 2. アクターを全部消す(ゲームのコードが作ったエンジンの型のコンポーネントも、vtable がゲームの DLL を
        //    指しうるので残さない)。メモリから消えるのは Detach の中の破棄の確定と GC。
        for (const core::ObjectHandle handle : scene.actors)
        {
            if (Actor* actor = registry.Resolve<Actor>(handle))
            {
                actor->Destroy();
            }
        }
        scene.RemoveDestroyedActors();

        // 3. 外す。外せなければ(ゲームの型のものが残っている)、シーンを戻してやめる
        if (!host.Detach(types, registry, app.GetAssetLoader()))
        {
            restoreScene();
            result.message = "Could not unload the game module: objects of game types remain (see the log)";
            return result;
        }

        // 4. ビルドし直した DLL の写しを読む。読めなければ前の写しに戻す
        bool restoredPrevious = false;
        if (!host.LoadFromLibrary(sourcePath, types))
        {
            restoredPrevious = host.RestorePreviousLibrary(types);
        }

        // 5. シーンを読み戻す(型を登録し直したので、ゲームの型のアクターも新しい DLL の型で作られる)
        restoreScene();
        if (restoredPrevious)
        {
            result.message = "Could not load the new game module; restored the previous one (see the log)";
            return result;
        }
        if (!host.IsAttached())
        {
            result.message = "Could not load the game module (see the log). Game types are missing";
            return result;
        }
        result.succeeded = true;
        result.message = "Reloaded the game module (" + std::to_string(actorCount) + " actors restored)";
        return result;
    }
}
