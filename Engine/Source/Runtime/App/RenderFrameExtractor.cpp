#include "RenderFrameExtractor.h"

#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Runtime/App/App.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

void RenderFrameExtractor::extract(const ExtractInput& input, RenderFrameData& outFrame)
{
    outFrame.clear();

    if (!input.scene) {
        return;
    }

    auto& reg = input.scene->getRegistry();

    extractCamera(input, outFrame);
    extractLights(reg, outFrame);
    extractSkybox(input.scene, outFrame);
    extractDrawItems(reg, outFrame.viewOwner, outFrame);
    sortDrawItems(outFrame.cameraPos, outFrame);
}

void RenderFrameExtractor::extractCamera(const ExtractInput& input, RenderFrameData& out)
{
    out.view           = input.view;
    out.projection     = input.projection;
    out.cameraPos      = input.cameraPos;
    out.viewportExtent = input.viewportExtent;
    out.viewOwner      = input.viewOwner;
    out.frameIndex     = input.frameIndex;
    out.deltaTime      = input.deltaTime;
}

void RenderFrameExtractor::extractLights(entt::registry& reg, RenderFrameData& out)
{
    // Directional light (take the first one with a transform)
    out.bHasDirectionalLight = false;
    for (const auto& [e, dlc, tc] : reg.view<DirectionalLightComponent, TransformComponent>().each()) {
        auto& dl         = out.directionalLight;
        dl.direction     = glm::normalize(tc.getForward());
        dl.ambient       = dlc._ambient;
        dl.diffuse       = dlc._diffuse;
        dl.specular      = dlc._specular;
        dl.view          = FMath::lookAt(glm::vec3(0.0f) - dl.direction * 50.0f, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        dl.projection    = FMath::orthographic(-40.f, 40.f, -40.f, 40.f, 0.1f, 200.f);
        dl.viewProjection = dl.projection * dl.view;
        out.bHasDirectionalLight = true;
        break;
    }

    // Fallback: directional light without transform
    if (!out.bHasDirectionalLight) {
        for (const auto& [e, dlc] : reg.view<DirectionalLightComponent>().each()) {
            auto& dl         = out.directionalLight;
            dl.direction     = glm::normalize(dlc._direction);
            dl.ambient       = dlc._ambient;
            dl.diffuse       = dlc._diffuse;
            dl.specular      = dlc._specular;
            dl.view          = FMath::lookAt(glm::vec3(0.0f) - dl.direction * 50.0f, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            dl.projection    = FMath::orthographic(-40.f, 40.f, -40.f, 40.f, 0.1f, 200.f);
            dl.viewProjection = dl.projection * dl.view;
            out.bHasDirectionalLight = true;
            break;
        }
    }

    // Point lights
    out.numPointLights = 0;
    for (const auto& [e, plc, tc] : reg.view<PointLightComponent, TransformComponent>().each()) {
        if (out.numPointLights >= MAX_POINT_LIGHTS) {
            break;
        }

        auto& pl       = out.pointLights[out.numPointLights];
        pl.type        = static_cast<float>(plc._type);
        pl.constant    = plc._constant;
        pl.linear      = plc._linear;
        pl.quadratic   = plc._quadratic;
        pl.position    = tc._position;
        pl.ambient     = plc._ambient;
        pl.diffuse     = plc._diffuse;
        pl.specular    = plc._specular;
        pl.spotDir     = tc.getForward();
        pl.innerCutOff = glm::cos(glm::radians(plc._innerConeAngle));
        pl.outerCutOff = glm::cos(glm::radians(plc._outerConeAngle));
        pl.nearPlane   = plc.nearPlane;
        pl.farPlane    = plc.farPlane;

        ++out.numPointLights;
    }

    std::sort(out.pointLights.begin(), out.pointLights.begin() + out.numPointLights,
              [&out](const FrameContext::PointLightData& lhs, const FrameContext::PointLightData& rhs)
              {
                  const glm::vec3 lhsDelta = lhs.position - out.cameraPos;
                  const glm::vec3 rhsDelta = rhs.position - out.cameraPos;
                  const float lhsDist2 = glm::dot(lhsDelta, lhsDelta);
                  const float rhsDist2 = glm::dot(rhsDelta, rhsDelta);
                  return lhsDist2 < rhsDist2;
              });
}

void RenderFrameExtractor::extractSkybox(Scene* scene, RenderFrameData& out)
{
    auto* resolver = App::get()->getResourceResolveSystem();
    if (!resolver) {
        return;
    }

    out.skybox.cubemapTexture    = resolver->findSceneSkyboxTextureShared(scene);
    out.skybox.irradianceTexture = resolver->findSceneEnvironmentIrradianceTextureShared(scene);
}

void RenderFrameExtractor::extractDrawItems(entt::registry& reg,
                                            entt::entity    viewOwner,
                                            RenderFrameData& out)
{
    // PBR
    for (const auto& [e, mc, tc, pmc] :
         reg.view<MeshComponent, TransformComponent, PBRMaterialComponent>().each())
    {
        if (e == viewOwner) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;

        auto* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;

        out.pbrDrawItems.push_back(RenderDrawItem{
            .worldMatrix   = tc.getTransform(),
            .mesh          = mc.getMesh(),
            .material      = mat,
            .materialIndex = static_cast<uint32_t>(mat->getIndex()),
            .sortKey       = 0.0f,
        });
    }

    // Phong
    for (const auto& [e, mc, tc, pmc] :
         reg.view<MeshComponent, TransformComponent, PhongMaterialComponent>().each())
    {
        if (e == viewOwner) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;

        auto* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;

        out.phongDrawItems.push_back(RenderDrawItem{
            .worldMatrix   = tc.getTransform(),
            .mesh          = mc.getMesh(),
            .material      = mat,
            .materialIndex = static_cast<uint32_t>(mat->getIndex()),
            .sortKey       = 0.0f,
        });
    }

    // Unlit
    for (const auto& [e, mc, tc, umc] :
         reg.view<MeshComponent, TransformComponent, UnlitMaterialComponent>().each())
    {
        if (e == viewOwner) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;

        auto* mat = umc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;

        out.unlitDrawItems.push_back(RenderDrawItem{
            .worldMatrix   = tc.getTransform(),
            .mesh          = mc.getMesh(),
            .material      = mat,
            .materialIndex = static_cast<uint32_t>(mat->getIndex()),
            .sortKey       = 0.0f,
        });
    }

    // Simple
    for (const auto& [e, mc, tc, smc] :
         reg.view<MeshComponent, TransformComponent, SimpleMaterialComponent>().each())
    {
        if (e == viewOwner) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;

        auto* mat = smc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;

        out.simpleDrawItems.push_back(RenderDrawItem{
            .worldMatrix   = tc.getTransform(),
            .mesh          = mc.getMesh(),
            .material      = mat,
            .materialIndex = static_cast<uint32_t>(mat->getIndex()),
            .sortKey       = 0.0f,
        });
    }

    // Fallback: MeshComponent + TransformComponent, no material
    // (entities that have a mesh but none of the material components above)
    for (const auto& [e, mc, tc] :
         reg.view<MeshComponent, TransformComponent>().each())
    {
        if (e == viewOwner) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;

        // Skip if it has any material component
        if (reg.any_of<PBRMaterialComponent, PhongMaterialComponent,
                        UnlitMaterialComponent, SimpleMaterialComponent>(e)) {
            continue;
        }

        out.fallbackDrawItems.push_back(RenderDrawItem{
            .worldMatrix   = tc.getTransform(),
            .mesh          = mc.getMesh(),
            .material      = nullptr,
            .materialIndex = 0,
            .sortKey       = 0.0f,
        });
    }
}

void RenderFrameExtractor::sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out)
{
    auto computeSortKey = [&cameraPos](RenderDrawItem& item) {
        glm::vec3 pos = glm::vec3(item.worldMatrix[3]);
        item.sortKey  = glm::distance2(cameraPos, pos);
    };

    auto sortByDistance = [](std::vector<RenderDrawItem>& items) {
        std::sort(items.begin(), items.end(), [](const RenderDrawItem& a, const RenderDrawItem& b) {
            return a.sortKey < b.sortKey;
        });
    };

    for (auto& item : out.pbrDrawItems)      computeSortKey(item);
    for (auto& item : out.phongDrawItems)    computeSortKey(item);
    for (auto& item : out.unlitDrawItems)    computeSortKey(item);
    for (auto& item : out.simpleDrawItems)   computeSortKey(item);
    for (auto& item : out.fallbackDrawItems) computeSortKey(item);

    sortByDistance(out.pbrDrawItems);
    sortByDistance(out.phongDrawItems);
    sortByDistance(out.unlitDrawItems);
    sortByDistance(out.simpleDrawItems);
    sortByDistance(out.fallbackDrawItems);
}

} // namespace ya
