#include "engine/CameraComponent.h"

#include "engine/Actor.h"
#include "engine/Input.h"
#include "engine/World.h"

namespace husky::engine
{
    CameraComponent* FindSceneCamera(World& scene)
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

    CameraView FindSceneCameraView(World& scene, float screenWidth, float screenHeight)
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

    CameraMatrices MakeSceneCamera(World& scene, float screenWidth, float screenHeight)
    {
        const CameraView view = FindSceneCameraView(scene, screenWidth, screenHeight);
        return MakeOrthographicCamera(view.centerX, view.centerY, view.zoom, screenWidth, screenHeight);
    }

    bool FindMouseWorldPosition(World& scene, core::Vector3& outWorld)
    {
        const Input* input = scene.GetInput();
        if (input == nullptr || input->GetScreenWidth() <= 0.0f || input->GetScreenHeight() <= 0.0f)
        {
            return false;
        }
        const float width = input->GetScreenWidth();
        const float height = input->GetScreenHeight();
        const core::Vector2 mouse = input->GetMousePosition();
        outWorld = ScreenToWorld(MakeSceneCamera(scene, width, height), mouse.x, mouse.y, width, height);
        return true;
    }
}
