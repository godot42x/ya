#include "RenderFrameExtractor.h"

#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/SkeletonAnimatorComponent.h"
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
    auto drawCtx = DrawItemExtractionContext{
        .registry  = &reg,
        .frameData = &outFrame,
        .viewOwner = outFrame.viewOwner,
    };
    extractDrawItems(drawCtx);
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
        auto& dl                 = out.directionalLight;
        dl.direction             = glm::normalize(tc.getForward());
        dl.view                  = FMath::lookAt(glm::vec3(0.0f) - dl.direction * 50.0f, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        dl.projection            = FMath::orthographic(-40.f, 40.f, -40.f, 40.f, 0.1f, 200.f);
        dl.viewProjection        = dl.projection * dl.view;
        dl.color                 = dlc._color;
        dl.intensity             = dlc.intensity;
        out.bHasDirectionalLight = true;
        break;
    }

    // Fallback: directional light without transform
    if (!out.bHasDirectionalLight) {
        for (const auto& [e, dlc] : reg.view<DirectionalLightComponent>().each()) {
            auto& dl                 = out.directionalLight;
            dl.direction             = glm::normalize(dlc._direction);
            dl.view                  = FMath::lookAt(glm::vec3(0.0f) - dl.direction * 50.0f, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            dl.projection            = FMath::orthographic(-40.f, 40.f, -40.f, 40.f, 0.1f, 200.f);
            dl.viewProjection        = dl.projection * dl.view;
            dl.color                 = dlc._color;
            dl.intensity             = dlc.intensity;
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
        pl.spotDir     = tc.getForward();
        pl.innerCutOff = glm::cos(glm::radians(plc._innerConeAngle));
        pl.outerCutOff = glm::cos(glm::radians(plc._outerConeAngle));
        pl.nearPlane   = plc.nearPlane;
        pl.farPlane    = plc.farPlane;
        pl.color       = plc.color;
        pl.intensity   = plc.intensity;

        ++out.numPointLights;
    }

    std::sort(out.pointLights.begin(), out.pointLights.begin() + out.numPointLights, [&out](const FrameContext::PointLightData& lhs, const FrameContext::PointLightData& rhs)
              {
                  const glm::vec3 lhsDelta = lhs.position - out.cameraPos;
                  const glm::vec3 rhsDelta = rhs.position - out.cameraPos;
                  const float lhsDist2 = glm::dot(lhsDelta, lhsDelta);
                  const float rhsDist2 = glm::dot(rhsDelta, rhsDelta);
                  return lhsDist2 < rhsDist2; });
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

int32_t RenderFrameExtractor::registerSkinningPalette(DrawItemExtractionContext& ctx,
                                                      entt::entity               entity,
                                                      Mesh*                      mesh)
{
    if (!ctx.registry || !ctx.frameData || !mesh || !mesh->hasSkinningVertexBuffer()) {
        return -1;
    }

    // SkinnedMeshComponent carries a pointer to the animator on the model-root
    // entity. Anything else cannot be skinned.
    auto* skinned = ctx.registry->try_get<SkinnedMeshComponent>(entity);
    if (!skinned || !skinned->_animator) {
        return -1;
    }
    SkeletonAnimatorComponent* skeletonComp = skinned->_animator;
    if (!skeletonComp->hasSkeleton()) {
        return -1;
    }

    const auto& pose = skeletonComp->getPose();
    if (pose.boneMatrices.empty()) {
        return -1;
    }

    auto& palette = ctx.frameData->skinningPalettes.emplace_back();
    YA_CORE_ASSERT(pose.boneMatrices.size() <= palette.boneMatrices.size(), "Exceed max bone size");
    const uint32_t boneCount = pose.boneMatrices.size();

    for (uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        palette.boneMatrices[boneIndex] = pose.boneMatrices[boneIndex];
    }

    return static_cast<int32_t>(ctx.frameData->skinningPalettes.size() - 1);
}

void RenderFrameExtractor::extractDrawItems(DrawItemExtractionContext& ctx)
{
    auto&      reg            = *ctx.registry;
    auto&      out            = *ctx.frameData;
    const auto viewOwner      = ctx.viewOwner;
    auto&      staticBuckets  = out.drawBuckets.staticMeshes;
    auto&      skinnedBuckets = out.drawBuckets.skinnedMeshes;

    // Emit a RenderDrawItem for every (MeshComp, TransformComponent, MaterialComp)
    // triple. Runs once per mesh component type (Static/Skinned) so both authoring
    // and model-instantiated entities feed into the same draw-item buckets.
    auto emitTyped = [&]<typename MeshComp, typename MatComp>(std::vector<RenderDrawItem>& bucket)
    {
        for (const auto& [e, mc, tc, matComp] :
             reg.view<MeshComp, TransformComponent, MatComp>().each()) {
            if (e == viewOwner) continue;
            if (!mc.isResolved() || !mc.getMesh()) continue;

            auto* mat = matComp.getMaterial();
            if (!mat || mat->getIndex() < 0) continue;

            bucket.push_back(RenderDrawItem{
                .worldMatrix          = tc.getTransform(),
                .mesh                 = mc.getMesh(),
                .material             = mat,
                .materialIndex        = static_cast<uint32_t>(mat->getIndex()),
                .sortKey              = 0.0f,
                .skinningPaletteIndex = registerSkinningPalette(ctx, e, mc.getMesh()),
            });
        }
    };

    emitTyped.template operator()<StaticMeshComponent, PBRMaterialComponent>(staticBuckets.pbrDrawItems);
    emitTyped.template operator()<StaticMeshComponent, PhongMaterialComponent>(staticBuckets.phongDrawItems);
    emitTyped.template operator()<StaticMeshComponent, UnlitMaterialComponent>(staticBuckets.unlitDrawItems);
    emitTyped.template operator()<StaticMeshComponent, SimpleMaterialComponent>(staticBuckets.simpleDrawItems);

    emitTyped.template operator()<SkinnedMeshComponent, PBRMaterialComponent>(skinnedBuckets.pbrDrawItems);
    emitTyped.template operator()<SkinnedMeshComponent, PhongMaterialComponent>(skinnedBuckets.phongDrawItems);
    emitTyped.template operator()<SkinnedMeshComponent, UnlitMaterialComponent>(skinnedBuckets.unlitDrawItems);
    emitTyped.template operator()<SkinnedMeshComponent, SimpleMaterialComponent>(skinnedBuckets.simpleDrawItems);

    // Fallback: mesh + transform, no material component. Run per mesh type and skip
    // entities that already carry any material component.
    auto emitFallback = [&]<typename MeshComp>(std::vector<RenderDrawItem>& bucket)
    {
        for (const auto& [e, mc, tc] :
             reg.view<MeshComp, TransformComponent>().each()) {
            if (e == viewOwner) continue;
            if (!mc.isResolved() || !mc.getMesh()) continue;

            if (reg.any_of<PBRMaterialComponent, PhongMaterialComponent, UnlitMaterialComponent, SimpleMaterialComponent>(e)) {
                continue;
            }

            bucket.push_back(RenderDrawItem{
                .worldMatrix          = tc.getTransform(),
                .mesh                 = mc.getMesh(),
                .material             = nullptr,
                .materialIndex        = 0,
                .sortKey              = 0.0f,
                .skinningPaletteIndex = registerSkinningPalette(ctx, e, mc.getMesh()),
            });
        }
    };

    emitFallback.template operator()<StaticMeshComponent>(staticBuckets.fallbackDrawItems);
    emitFallback.template operator()<SkinnedMeshComponent>(skinnedBuckets.fallbackDrawItems);
}

void RenderFrameExtractor::sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out)
{
    auto computeSortKey = [&cameraPos](RenderDrawItem& item)
    {
        glm::vec3 pos = glm::vec3(item.worldMatrix[3]);
        item.sortKey  = glm::distance2(cameraPos, pos);
    };

    auto sortByDistance = [](std::vector<RenderDrawItem>& items)
    {
        std::sort(items.begin(), items.end(), [](const RenderDrawItem& a, const RenderDrawItem& b)
                  { return a.sortKey < b.sortKey; });
    };

    auto sortBuckets = [&](RenderShadingDrawBuckets& buckets)
    {
        for (auto& item : buckets.pbrDrawItems) computeSortKey(item);
        for (auto& item : buckets.phongDrawItems) computeSortKey(item);
        for (auto& item : buckets.unlitDrawItems) computeSortKey(item);
        for (auto& item : buckets.simpleDrawItems) computeSortKey(item);
        for (auto& item : buckets.fallbackDrawItems) computeSortKey(item);

        sortByDistance(buckets.pbrDrawItems);
        sortByDistance(buckets.phongDrawItems);
        sortByDistance(buckets.unlitDrawItems);
        sortByDistance(buckets.simpleDrawItems);
        sortByDistance(buckets.fallbackDrawItems);
    };

    sortBuckets(out.drawBuckets.staticMeshes);
    sortBuckets(out.drawBuckets.skinnedMeshes);
}

} // namespace ya
