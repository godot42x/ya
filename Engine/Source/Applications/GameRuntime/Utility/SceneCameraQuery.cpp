#include "GameRuntime/Utility/SceneCameraQuery.h"

#include "ECS/Systems/Components/CameraComponent.h"
#include "Scene/Core/Scene.h"

namespace ya
{

Entity* findPrimaryCamera(Scene& scene)
{
    auto& registry = scene.getRegistry();

    Entity* anyCamera = nullptr;
    for (const auto& [handle, cameraComponent] : registry.view<CameraComponent>().each()) {
        if (cameraComponent.bPrimary) {
            return scene.getEntityByEnttID(handle);
        }
        if (!anyCamera) {
            anyCamera = scene.getEntityByEnttID(handle);
        }
    }
    return anyCamera;
}

Entity* findSecondaryCamera(Scene& scene, Entity* primaryCamera)
{
    auto& registry = scene.getRegistry();
    for (const auto& [handle, cameraComponent] : registry.view<CameraComponent>().each()) {
        (void)cameraComponent;
        Entity* entity = scene.getEntityByEnttID(handle);
        if (!entity || entity == primaryCamera) {
            continue;
        }
        return entity;
    }
    return nullptr;
}

} // namespace ya
