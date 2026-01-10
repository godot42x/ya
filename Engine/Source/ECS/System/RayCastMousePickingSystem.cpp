#include "RayCastMousePickingSystem.h"
#include "Core/Camera/Camera.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Render/Model.h"
#include "Scene/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

std::optional<RaycastHit> RayCastMousePickingSystem::raycast(Scene *scene, const Ray &ray)
{
    if (!scene) {
        return std::nullopt;
    }

    std::optional<RaycastHit> closestHit;
    float                     closestDistance = std::numeric_limits<float>::max();

    // Helper lambda to test material component
    auto testMaterialComponent = [&](entt::entity entityHandle, TransformComponent &tc, const auto &materialComp) {
        // Get first mesh for picking (简化版本)
        if (materialComp._meshes.empty()) {
            return;
        }

        for (auto *mesh : materialComp._meshes) {
            if (!mesh) {
                continue;
            }

            // Get world transform
            glm::mat4 worldTransform = tc.getTransform();

            auto worldAABB = mesh->boundingBox.transformed(worldTransform);

            // Test ray-AABB intersection
            float distance = 0.0f;
            if (ray.intersects(worldAABB, &distance))
            {
                // Check if this is the closest hit
                if (distance < closestDistance)
                {
                    closestDistance = distance;
                    closestHit      = RaycastHit{
                             .entity   = scene->getEntityByEnttID(entityHandle),
                             .distance = distance,
                             .point    = ray.origin + ray.direction * distance,
                    };
                }
            }
        }
    };

    // Check all material component types
    scene->getRegistry().view<SimpleMaterialComponent, TransformComponent>().each(
        [&](entt::entity handle, SimpleMaterialComponent &smc, TransformComponent &tc) {
            testMaterialComponent(handle, tc, smc);
        });

    scene->getRegistry().view<UnlitMaterialComponent, TransformComponent>().each(
        [&](entt::entity handle, UnlitMaterialComponent &umc, TransformComponent &tc) {
            testMaterialComponent(handle, tc, umc);
        });

    scene->getRegistry().view<LitMaterialComponent, TransformComponent>().each(
        [&](entt::entity handle, LitMaterialComponent &lmc, TransformComponent &tc) {
            testMaterialComponent(handle, tc, lmc);
        });

    return closestHit;
}

Entity *RayCastMousePickingSystem::pickEntity(
    Scene    *scene,
    float     screenX,
    float     screenY,
    float     viewportWidth,
    float     viewportHeight,
    glm::mat4 viewMatrix,
    glm::mat4 projectionMatrix)
{
    // Generate ray from screen coordinates
    Ray ray = Ray::fromScreen(
        screenX, screenY, viewportWidth, viewportHeight, viewMatrix, projectionMatrix);
    // Perform raycast
    auto hit = raycast(scene, ray);

    // Return hit entity or nullptr
    return hit.has_value() ? hit->entity : nullptr;
}

} // namespace ya
