#include "ResourceResolveSystem.h"
#include "Core/App/App.h"
#include "Scene/SceneManager.h"

#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"


namespace ya
{

void ResourceResolveSystem::onUpdate(float dt)
{
    // YA_PROFILE_FUNCTION_LOG();
    auto  sceneManager = App::get()->getSceneManager();
    auto *scene        = sceneManager->getActiveScene();
    if (!scene) return;

    auto &registry = scene->getRegistry();

    // TODO: use dirty list not iterating over all entities

    // 1. Resolve MeshComponents
    registry.view<MeshComponent>().each([&](auto entity, MeshComponent &meshComponent) {
        if (!meshComponent.isResolved() && meshComponent.hasMeshSource()) {
            meshComponent.resolve();
        }
    });

    // 2. Resolve LitMaterialComponents
    registry.view<LitMaterialComponent>().each([&](auto entity, LitMaterialComponent &materialComponent) {
        if (!materialComponent.isResolved()) {
            materialComponent.resolve();
        }
    });

    // Add more component types here as needed:
    // - PBRMaterialComponent
    // - SkeletalMeshComponent
    // - etc.
}

} // namespace ya
