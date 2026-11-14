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

    CameraView FindSceneCameraView(Scene& scene, float screenWidth, float screenHeight)
    {
        const CameraComponent* camera = FindSceneCamera(scene);
        const Actor* owner = camera != nullptr ? camera->GetOwner() : nullptr;
        if (owner == nullptr)
        {
            return CameraView{screenWidth * 0.5f, screenHeight * 0.5f, 1.0f}; // MakeScreenCamera と同じ見え方
        }
        const core::Vector3 position = owner->GetWorldPosition(); // 親がいてもワールドの位置を映す(HIER-1)
        return CameraView{position.x, position.y, camera->zoom};
    }

    CameraMatrices MakeSceneCamera(Scene& scene, float screenWidth, float screenHeight)
    {
        const CameraView view = FindSceneCameraView(scene, screenWidth, screenHeight);
        return MakeOrthographicCamera(view.centerX, view.centerY, view.zoom, screenWidth, screenHeight);
    }
}
