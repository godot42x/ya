#include "RayCastMousePickingSystem.h"
#include "Core/Camera/Camera.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Entity.h"
#include "Resource/Model.h"
#include "Scene/Core/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{
RayCastMousePickingSystem::AppStateProvider g_appStateProvider;
}

void RayCastMousePickingSystem::setAppStateProvider(AppStateProvider provider)
{
    g_appStateProvider = std::move(provider);
}

std::optional<RaycastHit> RayCastMousePickingSystem::raycast(Scene *scene, const Ray &ray)
{
    if (!scene) {
        return std::nullopt;
    }

    std::optional<RaycastHit> closestHit;
    float                     closestDistance = std::numeric_limits<float>::max();

    // Helper lambda to test mesh component (works for MeshComponent, StaticMeshComponent, SkinnedMeshComponent)
    auto testMeshComponent = [&](entt::entity entityHandle, TransformComponent &tc, auto &meshComp) {
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
    // Check all entities with any mesh component type
    auto& registry = scene->getRegistry();
    registry.view<StaticMeshComponent, TransformComponent>().each(
        [&](entt::entity handle, StaticMeshComponent &mc, TransformComponent &tc) {
            testMeshComponent(handle, tc, mc);
        });
    registry.view<SkinnedMeshComponent, TransformComponent>().each(
        [&](entt::entity handle, SkinnedMeshComponent &mc, TransformComponent &tc) {
            testMeshComponent(handle, tc, mc);
        });

    return closestHit;
}

std::optional<RaycastHit> RayCastMousePickingSystem::raycastBillboards(Scene* scene,
                                                                       const Ray& ray,
                                                                       const glm::mat4& viewMatrix,
                                                                       const glm::vec3& cameraPosition,
                                                                       float viewportHeight,
                                                                       AppState appState)
{
    if (!scene || viewportHeight <= 0.0f || appState != AppState::Stopped) {
        return std::nullopt;
    }

    auto& registry = scene->getRegistry();
    auto  view = registry.view<BillboardComponent, TransformComponent>();
    if (view.size_hint() == 0) {
        return std::nullopt;
    }

    const glm::vec3 cameraRight = glm::normalize(glm::vec3(viewMatrix[0]));
    const glm::vec3 cameraUp    = glm::normalize(glm::vec3(viewMatrix[1]));

    std::optional<RaycastHit> closestHit;
    float                     closestDistance = std::numeric_limits<float>::max();

    for (const auto& [entityHandle, billboard, transform] : view.each()) {
        if (!billboard.bVisible || !billboard.bManagedByLight) {
            continue;
        }

        Entity* entity = scene->getEntityByEnttID(entityHandle);
        if (!entity) {
            continue;
        }

        const glm::mat4 worldTransform = transform.getTransform();
        const glm::vec3 center         = glm::vec3(worldTransform[3]);
        const float distanceToCamera = glm::length(cameraPosition - center);
        if (distanceToCamera <= std::numeric_limits<float>::epsilon()) {
            continue;
        }

        const float screenSizePixels = std::max(billboard.screenSizePixels, 1.0f);
        const float scaleFactor = screenSizePixels / viewportHeight;
        const float halfExtent = std::max(billboard.minWorldScale, scaleFactor * distanceToCamera * 2.0f) * 0.5f;
        if (halfExtent <= 0.0f) {
            continue;
        }

        const glm::vec3 planeNormal = glm::normalize(center - cameraPosition);
        const float denom = glm::dot(ray.direction, planeNormal);
        if (std::abs(denom) <= 1e-5f) {
            continue;
        }

        const float distance = glm::dot(center - ray.origin, planeNormal) / denom;
        if (distance < 0.0f || distance >= closestDistance) {
            continue;
        }

        const glm::vec3 hitPoint = ray.at(distance);
        const glm::vec3 local    = hitPoint - center;
        const float x = glm::dot(local, cameraRight);
        const float y = glm::dot(local, cameraUp);
        if (std::abs(x) > halfExtent || std::abs(y) > halfExtent) {
            continue;
        }

        closestDistance = distance;
        closestHit      = RaycastHit{
            .entity   = entity,
            .distance = distance,
            .point    = hitPoint,
        };
    }

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
    Ray ray = Ray::fromScreen(
        screenX, screenY, viewportWidth, viewportHeight, viewMatrix, projectionMatrix);

    auto billboardHit = raycastBillboards(scene,
                                          ray,
                                          viewMatrix,
                                          glm::vec3(glm::inverse(viewMatrix)[3]),
                                          viewportHeight,
                                          g_appStateProvider ? g_appStateProvider() : AppState::Stopped);
    auto meshHit = raycast(scene, ray);

    if (billboardHit && (!meshHit || billboardHit->distance <= meshHit->distance)) {
        return billboardHit->entity;
    }

    return meshHit.has_value() ? meshHit->entity : nullptr;
}

} // namespace ya
