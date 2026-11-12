#include "engine/CameraComponent.h"

#include "engine/Actor.h"
#include "engine/Scene.h"

namespace husky::engine
{
    CameraComponent* FindSceneCamera(Scene& scene)
    {
        CameraComponent* found = nullptr;
        // ForEachActor は途中で止められないので、最初に見つけたものだけを覚えておく。
        scene.ForEachActor([&](Actor& actor) {
            if (found == nullptr)
            {
                found = actor.GetComponent<CameraComponent>();
            }
        });
        return found;
    }

    CameraMatrices MakeSceneCamera(Scene& scene, float screenWidth, float screenHeight)
    {
        const CameraComponent* camera = FindSceneCamera(scene);
        const Actor* owner = camera != nullptr ? camera->GetOwner() : nullptr;
        if (owner == nullptr)
        {
            return MakeScreenCamera(screenWidth, screenHeight);
        }
        const glm::vec3& position = owner->transform.position;
        return MakeOrthographicCamera(position.x, position.y, camera->zoom, screenWidth, screenHeight);
    }
}
