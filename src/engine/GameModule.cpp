// GameModule.h の実装。
#include "engine/GameModule.h"

#include "engine/EngineLog.h"
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
}
