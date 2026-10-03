#include "ECS/System/RayCastMousePickingSystem.h"
#include "Core/Camera/Camera.h"
#include "Render/Adapters/Companion/CompanionManager.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "Scene2D/Sprite2DComponent.h"
#include "ECS/Systems/TransformSystem.h"
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

    // Authored sprites are a local XY quad. Overlapping hits at the same
    // distance fall back to layer, then sortOrder, so the front sprite wins.
    struct SpritePick
    {
        Entity* entity = nullptr;
        float   distance = 0.0f;
        int32_t layer = 0;
        int32_t sortOrder = 0;
    };
    std::optional<SpritePick> bestSprite;
    registry.view<Sprite2DComponent, TransformComponent>().each(
        [&](entt::entity handle, Sprite2DComponent& sprite, TransformComponent& tc) {
            if (!sprite.bVisible || sprite.size.x <= 0.0f || sprite.size.y <= 0.0f) {
                return;
            }
            TransformSystem::computeWorldMatrix(&tc);
            const glm::mat4 world = tc.getTransform();
            if (std::abs(glm::determinant(world)) <= 1e-8f) {
                return;
            }
            const glm::mat4 inverse = glm::inverse(world);
            const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(ray.origin, 1.0f));
            const glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(ray.direction, 0.0f));
            if (std::abs(localDirection.z) <= 1e-6f) {
                return;
            }
            const float tLocal = -localOrigin.z / localDirection.z;
            if (tLocal < 0.0f) {
                return;
            }
            const glm::vec2 localHit{
                localOrigin.x + localDirection.x * tLocal,
                localOrigin.y + localDirection.y * tLocal,
            };
            if (std::abs(localHit.x) > sprite.size.x * 0.5f ||
                std::abs(localHit.y) > sprite.size.y * 0.5f) {
                return;
            }
            const glm::vec3 worldHit = glm::vec3(world * glm::vec4(localHit, 0.0f, 1.0f));
            const float distance = glm::dot(worldHit - ray.origin, ray.direction);
            if (distance < 0.0f) {
                return;
            }
            Entity* entity = scene->getEntityByEnttID(handle);
            if (!entity) {
                return;
            }
            const SpritePick candidate{
                .entity    = entity,
                .distance  = distance,
                .layer     = sprite.layer,
                .sortOrder = sprite.sortOrder,
            };
            if (!bestSprite) {
                bestSprite = candidate;
                return;
            }
            constexpr float kTie = 1e-3f;
            const bool bCloser = candidate.distance + kTie < bestSprite->distance;
            const bool bTie = std::abs(candidate.distance - bestSprite->distance) <= kTie;
            const bool bInFront = candidate.layer > bestSprite->layer ||
                                  (candidate.layer == bestSprite->layer &&
                                   candidate.sortOrder > bestSprite->sortOrder);
            if (bCloser || (bTie && bInFront)) {
                bestSprite = candidate;
            }
        });

    if (bestSprite && (!closestHit || bestSprite->distance < closestHit->distance)) {
        closestHit = RaycastHit{
            .entity   = bestSprite->entity,
            .distance = bestSprite->distance,
            .point    = ray.at(bestSprite->distance),
        };
    }

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
        // Only generated editor icons are pickable through this path (a
        // user-authored sprite is picked as ordinary content), and picking
        // must agree with drawing: the gizmo feature set is what the editor
        // view draws.
        if (!billboard.bVisible ||
            !rendersFeature(billboard.features, toMask(ERenderFeature::Gizmo))) {
            continue;
        }

        Entity* iconEntity = scene->getEntityByEnttID(entityHandle);
        if (!iconEntity) {
            continue;
        }

        // The icon is a generated companion, so a hit resolves to its host:
        // clicking a light icon selects the light.
        Entity* entity = CompanionManager::hostOf(*iconEntity);
        if (!entity) {
            entity = iconEntity;
        }

        const glm::mat4 worldTransform = transform.getTransform();
        const glm::vec3 center         = glm::vec3(worldTransform[3]);
        const float distanceToCamera = glm::length(cameraPosition - center);
        if (distanceToCamera <= std::numeric_limits<float>::epsilon()) {
            continue;
        }

        const float halfExtent = billboard.minWorldScale * 0.5f;
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
