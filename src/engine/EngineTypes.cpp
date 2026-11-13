// EngineTypes.h の実装。
#include "engine/EngineTypes.h"

#include "engine/Actor.h"
#include "engine/CameraComponent.h"
#include "engine/Collider2D.h"
#include "engine/Component.h"
#include "engine/Scene.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"

namespace husky::engine
{
    void RegisterEngineTypes(core::TypeRegistry& types)
    {
        types.Register<Scene>();
        types.Register<Actor>();
        types.Register<Component>();
        types.Register<SpriteComponent>();
        types.Register<SpriteAnimatorComponent>();
        types.Register<CameraComponent>();
        types.Register<BoxCollider2D>();    // Collider2D そのものは形が決まらないので登録しない
        types.Register<CircleCollider2D>();
    }
}
