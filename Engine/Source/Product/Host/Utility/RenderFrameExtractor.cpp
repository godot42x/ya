#include "RenderFrameExtractor.h"

#include "Host/App.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"

#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/SkeletonAnimatorComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/TransformSystem.h"
#include "Scene/Core/Scene.h"
#include "Render3D/Common/Shadow/Common/DirectionalShadowMath.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

glm::vec3 resolveDirectionalVector(TransformComponent* transform, const glm::vec3& fallbackDirection)
{
    if (transform) {
        TransformSystem::computeWorldMatrix(transform);
        const glm::vec3 forward = transform->getForward();
        if (glm::length2(forward) > std::numeric_limits<float>::epsilon()) {
            return glm::normalize(forward);
        }
    }

    if (glm::length2(fallbackDirection) > std::numeric_limits<float>::epsilon()) {
        return glm::normalize(fallbackDirection);
    }

    return glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::mat4 buildStableDirectionalShadowViewProjection(const glm::vec3& lightDirection,
                                                     const glm::vec3& cameraPosition,
                                                     const glm::mat4& cameraView,
                                                     float            shadowDistance,
                                                     uint32_t         shadowResolution)
{
    const float distance     = std::max(shadowDistance, 1.0f);
    const float radius       = distance;
    const float nearPlane    = 0.1f;
    const float farPlane     = std::max(distance * 4.0f, nearPlane + 1.0f);
    const float texelWorld   = (radius * 2.0f) / static_cast<float>(std::max(1u, shadowResolution));

    const glm::mat4 invView        = glm::inverse(cameraView);
    const glm::vec3 cameraForward  = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));
    const glm::vec3 focusCenter    = cameraPosition + cameraForward * (distance * 0.5f);
    const glm::vec3 worldUp        = std::abs(glm::dot(lightDirection, glm::vec3(0, 1, 0))) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::vec3 lightPosition  = focusCenter - lightDirection * (distance * 2.0f);

    glm::mat4 view = FMath::lookAt(lightPosition, focusCenter, worldUp);

    // Snap the shadow anchor in light space to texel units so camera motion does not shimmer.
    const glm::vec3 centerLightSpace = glm::vec3(view * glm::vec4(focusCenter, 1.0f));
    const glm::vec2 snappedXY        = glm::floor(glm::vec2(centerLightSpace) / texelWorld) * texelWorld;
    const glm::vec3 snapOffset       = glm::vec3(snappedXY - glm::vec2(centerLightSpace), 0.0f);
    view                             = glm::translate(glm::mat4(1.0f), snapOffset) * view;

    const glm::mat4 projection = FMath::orthographic(-radius, radius, -radius, radius, nearPlane, farPlane);
    return projection * view;
}

glm::mat4 buildDirectionalShadowViewProjection(const glm::vec3& lightDirection,
                                               const glm::vec3& cameraPosition,
                                               const glm::mat4& cameraView,
                                               const ShadowSettings& shadowSettings)
{
    if (shadowSettings.directionalStableFit) {
        return buildStableDirectionalShadowViewProjection(lightDirection,
                                                          cameraPosition,
                                                          cameraView,
                                                          shadowSettings.directionalDistance,
                                                          shadowSettings.resolution);
    }

    const float distance         = std::max(shadowSettings.directionalDistance, 1.0f);
    const glm::mat4 invView      = glm::inverse(cameraView);
    const glm::vec3 cameraForward = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));
    const glm::vec3 focusCenter  = cameraPosition + cameraForward * (distance * 0.5f);
    const glm::vec3 worldUp      = std::abs(glm::dot(lightDirection, glm::vec3(0, 1, 0))) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::vec3 lightPosition = focusCenter - lightDirection * (distance * 2.0f);
    const glm::mat4 view         = FMath::lookAt(lightPosition, focusCenter, worldUp);
    const glm::mat4 projection   = FMath::orthographic(-distance, distance, -distance, distance, 0.1f, std::max(distance * 4.0f, 1.1f));
    return projection * view;
}

} // namespace

void RenderFrameExtractor::extract(const ExtractInput& input, RenderFrameData& outFrame)
{
    outFrame.clear();

    if (!input.scene) {
        return;
    }

    auto& reg = input.scene->getRegistry();

    extractCamera(input, outFrame);
    extractLights(input, reg, outFrame);
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

void RenderFrameExtractor::extractLights(const ExtractInput& input, entt::registry& reg, RenderFrameData& out)
{
    const ShadowSettings defaultShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    const ShadowSettings& shadowSettings = input.shadowSettings ? *input.shadowSettings : defaultShadowSettings;
    const auto populateDirectionalShadow = [&](FrameContext::DirectionalLightData& light) {
        light.viewProjection = buildDirectionalShadowViewProjection(
            light.direction,
            input.cameraPos,
            input.view,
            shadowSettings);
        light.cascadeViewProjections[0] = light.viewProjection;
        light.cascadeSplits[0]          = shadowSettings.directionalDistance;
        light.cascadeCount              = 1;

        const uint32_t cascadeCount = shadowSettings.getEffectiveDirectionalCascadeCount();
        if (cascadeCount > 1) {
            const auto cascades = DirectionalShadowMath::buildCascades(
                light.direction,
                input.view,
                input.projection,
                shadowSettings.directionalDistance,
                shadowSettings.resolution,
                cascadeCount,
                shadowSettings.directionalStableFit,
                shadowSettings.directionalCascadeSplitRatios,
                shadowSettings.directionalDepthRangeMultiplier);
            light.cascadeViewProjections = cascades.viewProjections;
            light.cascadeSplits          = cascades.splits;
            light.cascadeCount           = cascades.count;
        }
        light.projection = glm::mat4(1.0f);
        light.view       = light.viewProjection;
    };

    // Directional light (take the first one with a transform)
    out.bHasDirectionalLight = false;
    for (const auto& [e, dlc, tc] : reg.view<DirectionalLightComponent, TransformComponent>().each()) {
        auto& dl                 = out.directionalLight;
        dl.direction             = resolveDirectionalVector(&tc, dlc._direction);
        populateDirectionalShadow(dl);
        dl.color                 = dlc._color;
        dl.intensity             = dlc.intensity;
        out.bHasDirectionalLight = true;
        break;
    }

    // Fallback: directional light without transform
    if (!out.bHasDirectionalLight) {
        for (const auto& [e, dlc] : reg.view<DirectionalLightComponent>().each()) {
            auto& dl                 = out.directionalLight;
            dl.direction             = resolveDirectionalVector(nullptr, dlc._direction);
            populateDirectionalShadow(dl);
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

    // Keep point-light order stable across camera motion so the shadow budget does not flicker
    // between different lights while the active view camera moves.
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

    if (auto it = ctx.skinningPaletteCache.find(skeletonComp); it != ctx.skinningPaletteCache.end()) {
        return it->second;
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

    const int32_t paletteIndex = static_cast<int32_t>(ctx.frameData->skinningPalettes.size() - 1);
    ctx.skinningPaletteCache.emplace(skeletonComp, paletteIndex);
    return paletteIndex;
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
                .entityId             = static_cast<uint32_t>(e),
                .sortKey              = 0.0f,
                .skinningPaletteIndex = registerSkinningPalette(ctx, e, mc.getMesh()),
            });
        }
    };

    emitTyped.template operator()<StaticMeshComponent, PBRMaterialComponent>(staticBuckets.pbrDrawItems);
    emitTyped.template operator()<StaticMeshComponent, PhongMaterialComponent>(staticBuckets.phongDrawItems);
    emitTyped.template operator()<StaticMeshComponent, UnlitMaterialComponent>(staticBuckets.unlitDrawItems);
    emitTyped.template operator()<StaticMeshComponent, SimpleMaterialComponent>(staticBuckets.simpleDrawItems);

    // Terrain draw items: mesh lives in the environment processor runtime
    // state, not on the component.
    auto* const envProcessor = App::get() ? App::get()->getEnvironmentLightingProcessor() : nullptr;
    if (envProcessor) {
        auto emitTerrain = [&]<typename MatComp>(std::vector<RenderDrawItem>& bucket)
        {
            for (const auto& [e, terrain, tc, matComp] :
                 reg.view<TerrainComponent, TransformComponent, MatComp>().each()) {
                if (e == viewOwner) continue;
                auto* mesh = envProcessor->getTerrainMesh(e);
                if (!mesh) continue;

                auto* mat = matComp.getMaterial();
                if (!mat || mat->getIndex() < 0) continue;

                bucket.push_back(RenderDrawItem{
                    .worldMatrix          = tc.getTransform(),
                    .mesh                 = mesh,
                    .material             = mat,
                    .materialIndex        = static_cast<uint32_t>(mat->getIndex()),
                    .entityId             = static_cast<uint32_t>(e),
                    .sortKey              = 0.0f,
                    .skinningPaletteIndex = registerSkinningPalette(ctx, e, mesh),
                });
            }
        };

        emitTerrain.template operator()<PBRMaterialComponent>(staticBuckets.pbrDrawItems);
        emitTerrain.template operator()<PhongMaterialComponent>(staticBuckets.phongDrawItems);
        emitTerrain.template operator()<UnlitMaterialComponent>(staticBuckets.unlitDrawItems);
        emitTerrain.template operator()<SimpleMaterialComponent>(staticBuckets.simpleDrawItems);
    }

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
                .entityId             = static_cast<uint32_t>(e),
                .sortKey              = 0.0f,
                .skinningPaletteIndex = registerSkinningPalette(ctx, e, mc.getMesh()),
            });
        }
    };

    emitFallback.template operator()<StaticMeshComponent>(staticBuckets.fallbackDrawItems);
    emitFallback.template operator()<SkinnedMeshComponent>(skinnedBuckets.fallbackDrawItems);

    // Terrain fallback: no material component
    if (envProcessor) {
        for (const auto& [e, terrain, tc] : reg.view<TerrainComponent, TransformComponent>().each()) {
            if (e == viewOwner) continue;
            auto* mesh = envProcessor->getTerrainMesh(e);
            if (!mesh) continue;
            if (reg.any_of<PBRMaterialComponent, PhongMaterialComponent, UnlitMaterialComponent, SimpleMaterialComponent>(e)) {
                continue;
            }

            staticBuckets.fallbackDrawItems.push_back(RenderDrawItem{
                .worldMatrix          = tc.getTransform(),
                .mesh                 = mesh,
                .material             = nullptr,
                .materialIndex        = 0,
                .entityId             = static_cast<uint32_t>(e),
                .sortKey              = 0.0f,
                .skinningPaletteIndex = registerSkinningPalette(ctx, e, mesh),
            });
        }
    }
}

void RenderFrameExtractor::sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out)
{
    auto computeSortKey = [&cameraPos](RenderDrawItem& item)
    {
        glm::vec3 pos = glm::vec3(item.worldMatrix[3]);
        item.sortKey  = glm::distance2(cameraPos, pos);
    };

    auto sortOpaqueBucket = [](std::vector<RenderDrawItem>& items)
    {
        std::sort(items.begin(), items.end(), [](const RenderDrawItem& a, const RenderDrawItem& b)
                  {
                      if (a.materialIndex != b.materialIndex) {
                          return a.materialIndex < b.materialIndex;
                      }
                      if (a.mesh != b.mesh) {
                          return a.mesh < b.mesh;
                      }
                      return a.sortKey < b.sortKey;
                  });
    };

    auto sortFallbackBucket = [](std::vector<RenderDrawItem>& items)
    {
        std::sort(items.begin(), items.end(), [](const RenderDrawItem& a, const RenderDrawItem& b)
                  {
                      if (a.mesh != b.mesh) {
                          return a.mesh < b.mesh;
                      }
                      return a.sortKey < b.sortKey;
                  });
    };

    auto sortBuckets = [&](RenderShadingDrawBuckets& buckets)
    {
        for (auto& item : buckets.pbrDrawItems) computeSortKey(item);
        for (auto& item : buckets.phongDrawItems) computeSortKey(item);
        for (auto& item : buckets.unlitDrawItems) computeSortKey(item);
        for (auto& item : buckets.simpleDrawItems) computeSortKey(item);
        for (auto& item : buckets.fallbackDrawItems) computeSortKey(item);

        sortOpaqueBucket(buckets.pbrDrawItems);
        sortOpaqueBucket(buckets.phongDrawItems);
        sortOpaqueBucket(buckets.unlitDrawItems);
        sortOpaqueBucket(buckets.simpleDrawItems);
        sortFallbackBucket(buckets.fallbackDrawItems);
    };

    sortBuckets(out.drawBuckets.staticMeshes);
    sortBuckets(out.drawBuckets.skinnedMeshes);
}

} // namespace ya
