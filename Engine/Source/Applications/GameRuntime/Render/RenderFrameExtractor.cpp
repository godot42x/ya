#include "GameRuntime/Render/RenderFrameExtractor.h"

#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"

#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "ECS/Systems/SkeletonAnimatorComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Scene3D/ManagedChildComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene/Core/Scene.h"
#include "Render/Adapters/Companion/CompanionManager.h"
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

void RenderFrameExtractor::extractSceneSnapshot(const SceneExtractInput& input, SceneSnapshot& outSnapshot)
{
    outSnapshot.clearScene();
    outSnapshot.bHasDirectionalLight = false;
    outSnapshot.pointLightSourceCount = 0;

    if (!input.scene) {
        return;
    }

    auto& registry = input.scene->getRegistry();
    extractSceneLights(registry, outSnapshot);
    auto drawCtx = DrawItemExtractionContext{
        .registry         = &registry,
        .sceneSnapshot    = &outSnapshot,
        .scene            = input.scene,
        .terrainProcessor = input.terrainProcessor,
    };
    extractDrawItems(drawCtx);
}

void RenderFrameExtractor::prepareView(const ViewPrepareInput& input,
                                       std::shared_ptr<const SceneSnapshot> sceneSnapshot,
                                       RenderFrameData& outFrame)
{
    outFrame.clear();
    if (!sceneSnapshot) {
        return;
    }

    outFrame.sceneSnapshot = std::move(sceneSnapshot);
    outFrame.viewFeatures  = input.viewFeatures;
    const auto bindBucket = [&](const std::vector<RenderDrawItem>& source, ViewDrawBucket& target)
    {
        target.source = &source;
        target.order.clear();
        target.order.reserve(source.size());
        for (uint32_t index = 0; index < source.size(); ++index) {
            const RenderDrawItem& item = source[index];

            // One gate for every consumer of a view's buckets: GBuffer,
            // Forward, shadow and the entity-id pick pass all read
            // frameData.drawBuckets, so this is the single place view
            // visibility is decided.
            if (!rendersFeature(item.features, input.viewFeatures)) {
                continue;
            }

            // A camera preview view must not draw the body of the camera it is
            // rendering through. A companion reports its host, because the
            // generated mesh is its own entity -- the host handle is what the
            // view owns.
            if (input.viewOwner != entt::null &&
                item.hostEntityId != 0 &&
                item.hostEntityId == static_cast<uint32_t>(input.viewOwner)) {
                continue;
            }
            target.order.push_back(index);
        }
    };
    const auto bindBuckets = [&](const RenderShadingDrawBuckets& source, ViewShadingDrawBuckets& target)
    {
        bindBucket(source.pbrDrawItems, target.pbrDrawItems);
        bindBucket(source.phongDrawItems, target.phongDrawItems);
        bindBucket(source.unlitDrawItems, target.unlitDrawItems);
        bindBucket(source.simpleDrawItems, target.simpleDrawItems);
        bindBucket(source.fallbackDrawItems, target.fallbackDrawItems);
    };
    bindBuckets(outFrame.sceneSnapshot->drawBuckets.staticMeshes, outFrame.drawBuckets.staticMeshes);
    bindBuckets(outFrame.sceneSnapshot->drawBuckets.skinnedMeshes, outFrame.drawBuckets.skinnedMeshes);
    outFrame.numPointLights = outFrame.sceneSnapshot->pointLightSourceCount;
    for (uint32_t index = 0; index < outFrame.sceneSnapshot->pointLightSourceCount; ++index) {
        const auto& source = outFrame.sceneSnapshot->pointLightSources[index];
        auto&       target = outFrame.pointLights[index];
        target.position    = source.position;
        target.type        = source.type;
        target.constant    = source.constant;
        target.linear      = source.linear;
        target.quadratic   = source.quadratic;
        target.color       = source.color;
        target.intensity   = source.intensity;
        target.spotDir     = source.spotDir;
        target.innerCutOff = source.innerCutOff;
        target.outerCutOff = source.outerCutOff;
        target.nearPlane   = source.nearPlane;
        target.farPlane    = source.farPlane;
    }
    outFrame.directionalLight.direction = outFrame.sceneSnapshot->directionalLightSource.direction;
    outFrame.directionalLight.color     = outFrame.sceneSnapshot->directionalLightSource.color;
    outFrame.directionalLight.intensity = outFrame.sceneSnapshot->directionalLightSource.intensity;
    extractCamera(input, outFrame);
    prepareViewLights(input, outFrame);
    sortDrawItems(outFrame.cameraPos, outFrame);
}

void RenderFrameExtractor::extractCamera(const ViewPrepareInput& input, RenderFrameData& out)
{
    out.view           = input.view;
    out.projection     = input.projection;
    out.viewProjection = input.viewProjection;
    out.cameraPos      = input.cameraPos;
    out.viewExtent = input.viewExtent;
    out.viewOwner      = input.viewOwner;
    out.frameIndex     = input.frameIndex;
    out.deltaTime      = input.deltaTime;
    out.timeSeconds    = input.elapsedTimeSeconds;
}

void RenderFrameExtractor::extractSceneLights(entt::registry& reg, SceneSnapshot& out)
{
    // Directional light (take the first one with a transform)
    out.bHasDirectionalLight = false;
    for (const auto& [e, dlc, tc] : reg.view<DirectionalLightComponent, TransformComponent>().each()) {
        auto& dl                 = out.directionalLightSource;
        dl.direction             = resolveDirectionalVector(&tc, dlc._direction);
        dl.color                 = dlc._color;
        dl.intensity             = dlc.intensity;
        out.bHasDirectionalLight = true;
        break;
    }

    // Fallback: directional light without transform
    if (!out.bHasDirectionalLight) {
        for (const auto& [e, dlc] : reg.view<DirectionalLightComponent>().each()) {
            auto& dl                 = out.directionalLightSource;
            dl.direction             = resolveDirectionalVector(nullptr, dlc._direction);
            dl.color                 = dlc._color;
            dl.intensity             = dlc.intensity;
            out.bHasDirectionalLight = true;
            break;
        }
    }

    // Point lights
    out.pointLightSourceCount = 0;
    for (const auto& [e, plc, tc] : reg.view<PointLightComponent, TransformComponent>().each()) {
        if (out.pointLightSourceCount >= MAX_POINT_LIGHTS) {
            break;
        }

        auto& pl       = out.pointLightSources[out.pointLightSourceCount];
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

        ++out.pointLightSourceCount;
    }

    // Keep point-light order stable across camera motion so the shadow budget does not flicker
    // between different lights while the active view camera moves.
}

void RenderFrameExtractor::prepareViewLights(const ViewPrepareInput& input, RenderFrameData& out)
{
    if (!out.sceneSnapshot || !out.sceneSnapshot->bHasDirectionalLight) {
        return;
    }

    const ShadowSettings defaultShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    const ShadowSettings& shadowSettings = input.shadowSettings ? *input.shadowSettings : defaultShadowSettings;
    auto& light = out.directionalLight;
    light.viewProjection = buildDirectionalShadowViewProjection(
        light.direction, input.cameraPos, input.view, shadowSettings);
    light.cascadeViewProjections[0] = light.viewProjection;
    light.cascadeSplits[0] = shadowSettings.directionalDistance;
    light.cascadeCount = 1;

    const uint32_t cascadeCount = shadowSettings.getEffectiveDirectionalCascadeCount();
    if (cascadeCount > 1) {
        const auto cascades = DirectionalShadowMath::buildCascades(
            light.direction, input.view, input.projection, shadowSettings.directionalDistance,
            shadowSettings.resolution, cascadeCount, shadowSettings.directionalStableFit,
            shadowSettings.directionalCascadeSplitRatios, shadowSettings.directionalDepthRangeMultiplier);
        light.cascadeViewProjections = cascades.viewProjections;
        light.cascadeSplits = cascades.splits;
        light.cascadeCount = cascades.count;
    }
    light.projection = glm::mat4(1.0f);
    light.view = light.viewProjection;
}

int32_t RenderFrameExtractor::registerSkinningPalette(DrawItemExtractionContext& ctx,
                                                      entt::entity               entity,
                                                      Mesh*                      mesh)
{
    if (!ctx.registry || !ctx.sceneSnapshot || !mesh || !mesh->hasSkinningVertexBuffer()) {
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

    auto& palette = ctx.sceneSnapshot->skinningPalettes.emplace_back();
    YA_CORE_ASSERT(pose.boneMatrices.size() <= palette.boneMatrices.size(), "Exceed max bone size");
    const uint32_t boneCount = pose.boneMatrices.size();

    for (uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        palette.boneMatrices[boneIndex] = pose.boneMatrices[boneIndex];
    }

    const int32_t paletteIndex = static_cast<int32_t>(ctx.sceneSnapshot->skinningPalettes.size() - 1);
    ctx.skinningPaletteCache.emplace(skeletonComp, paletteIndex);
    return paletteIndex;
}

void RenderFrameExtractor::extractDrawItems(DrawItemExtractionContext& ctx)
{
    auto&      reg            = *ctx.registry;
    auto&      out            = *ctx.sceneSnapshot;
    auto&      staticBuckets  = out.drawBuckets.staticMeshes;
    auto&      skinnedBuckets = out.drawBuckets.skinnedMeshes;

    // View visibility is policy, not component state: authored content keeps
    // the `Game` default while a generated companion takes the feature set and
    // host declared for its host component. Only companions pay the lookup.
    const auto tagCompanion = [&](RenderDrawItem& item, entt::entity entity)
    {
        if (!reg.all_of<ManagedChildComponent>(entity)) {
            return;
        }

        Entity* owner = ctx.scene ? ctx.scene->getEntityByEnttID(entity) : nullptr;
        if (!owner || !owner->isValid()) {
            return;
        }

        item.features     = CompanionManager::featureMaskOf(*owner);
        item.hostEntityId = CompanionManager::hostEntityIdOf(*owner);
    };

    // Emit a RenderDrawItem for every (MeshComp, TransformComponent, MaterialComp)
    // triple. Runs once per mesh component type (Static/Skinned) so both authoring
    // and model-instantiated entities feed into the same draw-item buckets.
    auto emitTyped = [&]<typename MeshComp, typename MatComp>(std::vector<RenderDrawItem>& bucket)
    {
        for (const auto& [e, mc, tc, matComp] :
            reg.view<MeshComp, TransformComponent, MatComp>().each()) {
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
            tagCompanion(bucket.back(), e);
        }
    };

    emitTyped.template operator()<StaticMeshComponent, PBRMaterialComponent>(staticBuckets.pbrDrawItems);
    emitTyped.template operator()<StaticMeshComponent, PhongMaterialComponent>(staticBuckets.phongDrawItems);
    emitTyped.template operator()<StaticMeshComponent, UnlitMaterialComponent>(staticBuckets.unlitDrawItems);
    emitTyped.template operator()<StaticMeshComponent, SimpleMaterialComponent>(staticBuckets.simpleDrawItems);

    // Terrain draw items: mesh lives in the terrain processor runtime state,
    // not on the component.
    auto* const terrainProcessor = ctx.terrainProcessor;
    if (terrainProcessor && ctx.scene) {
        auto emitTerrain = [&]<typename MatComp>(std::vector<RenderDrawItem>& bucket)
        {
            for (const auto& [e, terrain, tc, matComp] :
                 reg.view<TerrainComponent, TransformComponent, MatComp>().each()) {
                auto* mesh = terrainProcessor->getTerrainMesh(*ctx.scene, e);
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
                tagCompanion(bucket.back(), e);
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
            tagCompanion(bucket.back(), e);
        }
    };

    emitFallback.template operator()<StaticMeshComponent>(staticBuckets.fallbackDrawItems);
    emitFallback.template operator()<SkinnedMeshComponent>(skinnedBuckets.fallbackDrawItems);

    // Terrain fallback: no material component
    if (terrainProcessor && ctx.scene) {
        for (const auto& [e, terrain, tc] : reg.view<TerrainComponent, TransformComponent>().each()) {
            auto* mesh = terrainProcessor->getTerrainMesh(*ctx.scene, e);
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
            tagCompanion(staticBuckets.fallbackDrawItems.back(), e);
        }
    }
}

void RenderFrameExtractor::sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out)
{
    const auto distanceToCamera = [&cameraPos](const RenderDrawItem& item)
    {
        return glm::distance2(cameraPos, glm::vec3(item.worldMatrix[3]));
    };

    const auto sortOpaqueBucket = [&](ViewDrawBucket& bucket)
    {
        std::sort(bucket.order.begin(), bucket.order.end(), [&](uint32_t lhs, uint32_t rhs)
                  {
                      const auto& a = (*bucket.source)[lhs];
                      const auto& b = (*bucket.source)[rhs];
                      if (a.materialIndex != b.materialIndex) {
                          return a.materialIndex < b.materialIndex;
                      }
                      if (a.mesh != b.mesh) {
                          return a.mesh < b.mesh;
                      }
                      return distanceToCamera(a) < distanceToCamera(b);
                  });
    };

    const auto sortFallbackBucket = [&](ViewDrawBucket& bucket)
    {
        std::sort(bucket.order.begin(), bucket.order.end(), [&](uint32_t lhs, uint32_t rhs)
                  {
                      const auto& a = (*bucket.source)[lhs];
                      const auto& b = (*bucket.source)[rhs];
                      if (a.mesh != b.mesh) {
                          return a.mesh < b.mesh;
                      }
                      return distanceToCamera(a) < distanceToCamera(b);
                  });
    };

    auto sortBuckets = [&](ViewShadingDrawBuckets& buckets)
    {
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
