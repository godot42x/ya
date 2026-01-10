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

        auto *mesh = materialComp._meshes[0];
        if (!mesh) {
            return;
        }

        // Get world transform
        glm::mat4 worldTransform = tc.getTransform();

        // TODO: MaterialComponent should reference Model for proper AABB
        // Currently using a default unit cube AABB as fallback
        AABB localAABB;
        localAABB.min = glm::vec3(-1.0f, -1.0f, -1.0f);
        localAABB.max = glm::vec3(1.0f, 1.0f, 1.0f);

        // Transform AABB to world space (only center point for simplicity)
        glm::vec3 center = (localAABB.min + localAABB.max) * 0.5f;
        glm::vec3 extent = (localAABB.max - localAABB.min) * 0.5f;

        glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(center, 1.0f));

        // Extract scale from transform
        glm::vec3 scale = glm::vec3(
            glm::length(glm::vec3(worldTransform[0])),
            glm::length(glm::vec3(worldTransform[1])),
            glm::length(glm::vec3(worldTransform[2])));

        glm::vec3 worldExtent = extent * scale;
        glm::vec3 aabbMin     = worldCenter - worldExtent;
        glm::vec3 aabbMax     = worldCenter + worldExtent;

        // Test ray-AABB intersection
        float distance = 0.0f;
        AABB  worldAABB{aabbMin, aabbMax};
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
