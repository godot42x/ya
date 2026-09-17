#include "Render/Adapters/Companion/RenderCompanionSpecs.h"

#include "Core/Log.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ya
{

namespace
{

constexpr const char* kCameraBodySuffix = "_CameraBody";
constexpr const char* kLightIconSuffix  = "_LightIcon";

/// Companion nodes are children of the host node, so the host transform (and
/// every later move or rotation) applies through the node hierarchy without
/// the companion tracking anything itself.
Entity* createCompanionNode(Scene& scene, Entity& host, const char* suffix)
{
    Node* hostNode = scene.getNodeByEntity(&host);
    if (!hostNode) {
        return nullptr;
    }

    Node* node = scene.createNode3D(std::format("{}{}", host.getName(), suffix), hostNode);
    return node ? node->getEntity() : nullptr;
}

/// Editor visuals read as flat tinted shapes: one unlit colour, no texture.
void applyUnlitTint(UnlitMaterialComponent& unlit, const glm::vec4& color)
{
    unlit._params.baseColor0 = glm::vec3(color);
    unlit._params.baseColor1 = glm::vec3(color);
    unlit._params.mixValue   = 0.0f;
    unlit.invalidate();
}

void applyBillboardDefaults(BillboardComponent& billboard, const LightBillboardConfig& config)
{
    // Declaration owns the boundary: a light icon is an editor companion, so it
    // belongs to the gizmo feature set and only gizmo-drawing views show it.
    billboard.features         = renderFeatureMaskOf(ECompanionKind::EditorGizmo);
    billboard.bVisible         = config.enabled;
    billboard.screenSizePixels = config.screenSizePixels;
    billboard.minWorldScale    = config.minWorldScale;
    billboard.tint             = config.tint;
    if (!billboard.image.hasPath() && billboard.image.textureRef.getPath() != config.texturePath) {
        billboard.image.textureRef.setPathWithoutNotify(config.texturePath);
    }
    billboard.invalidate();
}

/// Host orientation, falling back to the component's authored direction.
glm::vec3 resolveHostForward(entt::registry& reg, entt::entity entity, const glm::vec3& fallback)
{
    if (auto* transform = reg.try_get<TransformComponent>(entity)) {
        TransformSystem::computeWorldMatrix(transform);
        const glm::vec3 forward = transform->getForward();
        if (glm::length2(forward) > std::numeric_limits<float>::epsilon()) {
            return glm::normalize(forward);
        }
    }

    if (glm::length2(fallback) > std::numeric_limits<float>::epsilon()) {
        return glm::normalize(fallback);
    }
    return glm::vec3(0.0f, 0.0f, -1.0f);
}

/// Shared shape of both light companions: a billboard child whose tint and
/// orientation follow the host light.
CompanionSpec makeLightCompanionSpec(const LightBillboardConfig& config, bool bDirectional)
{
    CompanionSpec spec;
    spec.kind                  = ECompanionKind::EditorGizmo;
    spec.bAuthorEditable       = false;
    spec.packClass             = EAssetPackClass::EditorOnly;
    // Rotating a directional light changes the icon's orientation, so host
    // transform changes have to refresh it.
    spec.bFollowsHostTransform = true;

    spec.onCreate = [config](Scene& scene, Entity& host) -> Entity* {
        Entity* companion = createCompanionNode(scene, host, kLightIconSuffix);
        if (!companion) {
            return nullptr;
        }

        auto* billboard = companion->addComponent<BillboardComponent>();
        if (!billboard) {
            return nullptr;
        }
        applyBillboardDefaults(*billboard, config);
        return companion;
    };

    spec.onUpdateHost = [config, bDirectional](Scene& scene, Entity& host, Entity& companion) {
        if (!companion.hasComponent<BillboardComponent>()) {
            return;
        }
        auto* billboard = companion.getComponent<BillboardComponent>();

        applyBillboardDefaults(*billboard, config);

        entt::registry& reg    = scene.getRegistry();
        const auto      handle = host.getHandle();

        if (bDirectional) {
            const auto* light = reg.try_get<DirectionalLightComponent>(handle);
            if (!light) {
                return;
            }
            billboard->tint           = glm::vec4(light->_color * std::max(light->intensity, 0.2f), 1.0f);
            billboard->worldDirection = resolveHostForward(reg, handle, light->_direction);
        }
        else {
            const auto* light = reg.try_get<PointLightComponent>(handle);
            if (!light) {
                return;
            }
            billboard->tint           = glm::vec4(light->color * std::max(light->intensity, 0.2f), 1.0f);
            billboard->worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        billboard->invalidate();
    };

    return spec;
}

} // namespace

CompanionSpec makeCameraCompanionSpec(const CameraCompanionPolicy& policy)
{
    CompanionSpec spec;
    spec.kind            = ECompanionKind::EditorGizmo;
    spec.bAuthorEditable = false;
    spec.packClass       = EAssetPackClass::EditorOnly;

    spec.onCreate = [policy](Scene& scene, Entity& host) -> Entity* {
        Entity* companion = createCompanionNode(scene, host, kCameraBodySuffix);
        if (!companion) {
            return nullptr;
        }

        if (auto* mesh = companion->addComponent<StaticMeshComponent>()) {
            mesh->_mesh.setEngineMesh(EEngineMesh::CameraBody);
            mesh->invalidate();
        }
        if (auto* unlit = companion->addComponent<UnlitMaterialComponent>()) {
            applyUnlitTint(*unlit, policy.baseColor);
        }
        if (auto* transform = companion->getComponent<TransformComponent>()) {
            transform->setScale(glm::vec3(policy.scale));
        }
        return companion;
    };

    return spec;
}

CompanionSpec makePointLightCompanionSpec(const LightBillboardConfig& config)
{
    return makeLightCompanionSpec(config, false);
}

CompanionSpec makeDirectionalLightCompanionSpec(const LightBillboardConfig& config)
{
    return makeLightCompanionSpec(config, true);
}

} // namespace ya
