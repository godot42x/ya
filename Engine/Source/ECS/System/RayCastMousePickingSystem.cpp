#include "RayCastMousePickingSystem.h"
#include "Core/Camera/Camera.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
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

    // Helper lambda to test mesh component
    auto testMeshComponent = [&](entt::entity entityHandle, TransformComponent &tc, MeshComponent &meshComp) {
        Mesh* mesh = meshComp.getMesh();
        if (!mesh) {
            return;
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
    };

    // TODO: how to apply material's logic transform to the mesh in the world?
    //      经过材质处理，mesh的实际大小位置可能发生变化
    // Check all entities with MeshComponent
    scene->getRegistry().view<MeshComponent, TransformComponent>().each(
        [&](entt::entity handle, MeshComponent &mc, TransformComponent &tc) {
            testMeshComponent(handle, tc, mc);
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
