// EngineTypes.h の実装。
#include "engine/EngineTypes.h"

#include "engine/Actor.h"
#include "engine/Component.h"
#include "engine/Scene.h"
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    void RegisterEngineTypes(core::TypeRegistry& types)
    {
        types.Register<Scene>();
        types.Register<Actor>();
        types.Register<Component>();
        types.Register<SpriteComponent>();
    }
}
