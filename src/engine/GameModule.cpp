// GameModule.h の実装。
#include "engine/GameModule.h"

#include "core/ObjectRegistry.h"
#include "core/TypeRegistry.h"
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"
#include "engine/Prefab.h"
#include "platform/DynamicLibrary.h"

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

    bool GameModuleHost::LoadFromLibrary(const std::string& path, core::TypeRegistry& types)
    {
        GameModule module;
        if (!LoadGameModule(m_library, path, module))
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
        loader.ForgetScriptableData(m_types);
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
            HUSKY_LOG(ModuleLog::Unloaded, path);
        }
        return true;
    }
}
