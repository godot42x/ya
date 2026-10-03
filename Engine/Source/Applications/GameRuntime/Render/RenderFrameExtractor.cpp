#include "GameRuntime/Render/RenderFrameExtractor.h"

#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"

#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/TilemapComponent.h"
#include "Render3D/Common/TilemapExtraction.h"
#include "Render3D/Common/TilemapExtraction.h"
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
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "Scene/Core/Scene.h"
#include "Render/Adapters/Companion/CompanionManager.h"
#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Common/Shadow/Common/DirectionalShadowMath.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

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

/// View visibility is policy, not component state: authored content keeps the
/// `Game` default while a generated companion takes the feature set and host
/// declared for its host component. Only companions pay the lookup.
void tagCompanionFeatures(Scene*           scene,
                          entt::registry&  reg,
                          entt::entity     entity,
                          FRenderFeatureMask& features,
                          uint32_t&        hostEntityId)
{
    if (!reg.all_of<ManagedChildComponent>(entity)) {
        return;
    }

    Entity* owner = scene ? scene->getEntityByEnttID(entity) : nullptr;
    if (!owner || !owner->isValid()) {
        return;
    }

    features     = CompanionManager::featureMaskOf(*owner);
    hostEntityId = CompanionManager::hostEntityIdOf(*owner);
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
    extractSprites(input.scene, registry, outSnapshot);
    extractTilemaps(input.scene, registry, outSnapshot);
    // Texel grid of the primary camera. Zoom is per view and is applied when
    // that view snaps its eye; this grid is the one every integer zoom shares.
    float texelStep = 0.0f;
    if (Entity* camera = findPrimaryCamera(*input.scene)) {
        if (const auto* component = camera->getComponent<CameraComponent>();
            component && component->_pixelPerfect && component->_pixelsPerUnit > 0.0f) {
            texelStep = 1.0f / component->_pixelsPerUnit;
        }
    }
    snapSpriteCandidatesToTexelGrid(outSnapshot.worldSprites, texelStep);
    auto drawCtx = DrawItemExtractionContext{
        .registry         = &registry,
        .sceneSnapshot    = &outSnapshot,
        .scene            = input.scene,
        .terrainProcessor = input.terrainProcessor,
    };
    extractDrawItems(drawCtx);
}

WorldSpriteCandidate RenderFrameExtractor::buildSpriteCandidate(const glm::mat4&         world,
                                                               const Sprite2DComponent& sprite,
                                                               uint32_t                 entityId)
{
    WorldSpriteCandidate candidate{};
    // The quad is the entity's local XY rectangle scaled by the authored size,
    // so the world axes carry rotation, scale and size together. The pivot
    // sits on the entity; `worldCenter` is the quad's centre.
    candidate.axisX = glm::vec3(world[0]) * sprite.size.x;
    candidate.axisY = glm::vec3(world[1]) * sprite.size.y;
    const glm::vec2 centerOffset = spriteQuadCenterOffset(sprite);
    candidate.worldCenter = glm::vec3(world[3])
                          + glm::vec3(world[0]) * centerOffset.x
                          + glm::vec3(world[1]) * centerOffset.y;
    candidate.uvRect = sprite.uvRect;
    if (sprite.bFlipU) {
        std::swap(candidate.uvRect.x, candidate.uvRect.z);
    }
    if (sprite.bFlipV) {
        std::swap(candidate.uvRect.y, candidate.uvRect.w);
    }
    candidate.tint = sprite.tint;
    // Only a slot that named a texture resolves to one: an unnamed slot must not
    // ask the texture library for its white stand-in, which is the same "no
    // substitute image" rule `spriteIsDrawable` applies one layer up.
    if (sprite.image.hasPath()) {
        candidate.texture = slotToTextureBinding(sprite.image);
    }
    candidate.entityId     = entityId;
    candidate.layer        = sprite.layer;
    candidate.sortOrder    = sprite.sortOrder;
    candidate.bTranslucent = sprite.tint.a < 1.0f;
    return candidate;
}

void RenderFrameExtractor::snapSpriteCandidatesToTexelGrid(std::vector<WorldSpriteCandidate>& sprites, float texelStep)
{
    if (!(texelStep > 0.0f)) {
        return;
    }
    for (WorldSpriteCandidate& sprite : sprites) {
        sprite.worldCenter = snapWorldXY(sprite.worldCenter, texelStep, 0.0f, 0.0f);
    }
}

void RenderFrameExtractor::extractSprites(Scene* scene, entt::registry& reg, SceneSnapshot& out)
{
    for (const auto& [entity, sprite, transform] : reg.view<Sprite2DComponent, TransformComponent>().each()) {
        // Unset, loading and failed textures are not drawn, and there is no
        // substitute image: the component owns that rule, and this is the only
        // place a sprite becomes a draw candidate.
        if (!spriteIsDrawable(sprite)) {
            continue;
        }
        if (sprite.size.x <= 0.0f || sprite.size.y <= 0.0f) {
            continue;
        }

        TransformSystem::computeWorldMatrix(&transform);
        WorldSpriteCandidate candidate =
            buildSpriteCandidate(transform.getTransform(), sprite, static_cast<uint32_t>(entity));
        tagCompanionFeatures(scene, reg, entity, candidate.features, candidate.hostEntityId);
        out.worldSprites.push_back(std::move(candidate));
    }
}

void RenderFrameExtractor::extractTilemaps(Scene* scene, entt::registry& reg, SceneSnapshot& out)
{
    for (const auto& [entity, tilemap, transform] : reg.view<TilemapComponent, TransformComponent>().each()) {
        // Same "no substitute image" rule as sprites: an unloadable tileset
        // or a still-loading atlas yields no candidates, never placeholders.
        if (!tilemap.isValid() || !tilemap.tileset.isLoaded()) {
            continue;
        }
        const Tileset* tileset = tilemap.tileset.get();
        if (!tileset || !tileset->atlas.isReady()) {
            continue;
        }
        const Texture* atlasTexture = tileset->atlas.textureRef.get();
        if (!atlasTexture || atlasTexture->getWidth() == 0 || atlasTexture->getHeight() == 0) {
            continue;
        }

        TransformSystem::computeWorldMatrix(&transform);
        const TextureBinding binding = slotToTextureBinding(tileset->atlas);
        const size_t first = out.worldSprites.size();
        appendTilemapCandidates(TilemapExtractionInput{
            .map           = &tilemap,
            .tileset       = tileset,
            .world         = transform.getTransform(),
            .entityId      = static_cast<uint32_t>(entity),
            .atlas         = &binding,
            .textureWidth  = atlasTexture->getWidth(),
            .textureHeight = atlasTexture->getHeight(),
        },
                                out.worldSprites);
        for (size_t index = first; index < out.worldSprites.size(); ++index) {
            tagCompanionFeatures(scene, reg, entity, out.worldSprites[index].features,
                                 out.worldSprites[index].hostEntityId);
        }
    }
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

    // Sprites pass the same two gates as mesh candidates: what the component
    // declares it belongs to, and whether this View is allowed to see the
    // entity it came from.
    const auto& spriteSource = outFrame.sceneSnapshot->worldSprites;
    outFrame.worldSprites.source = &spriteSource;
    outFrame.worldSprites.order.clear();
    outFrame.worldSprites.order.reserve(spriteSource.size());
    for (uint32_t index = 0; index < spriteSource.size(); ++index) {
        const WorldSpriteCandidate& sprite = spriteSource[index];
        if (!rendersFeature(sprite.features, input.viewFeatures)) {
            continue;
        }
        if (input.viewOwner != entt::null &&
            sprite.hostEntityId != 0 &&
            sprite.hostEntityId == static_cast<uint32_t>(input.viewOwner)) {
            continue;
        }
        outFrame.worldSprites.order.push_back(index);
    }

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
    sortViewBuckets(outFrame.cameraPos, outFrame);
}

void RenderFrameExtractor::extractCamera(const ViewPrepareInput& input, RenderFrameData& out)
{
    out.view           = input.view;
    out.projection     = input.projection;
    out.viewProjection = input.viewProjection;
    out.cameraPos      = input.cameraPos;
    out.viewExtent = input.viewExtent;
    out.viewOwner      = input.viewOwner;
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
        tagCompanionFeatures(ctx.scene, reg, entity, item.features, item.hostEntityId);
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

void RenderFrameExtractor::sortViewBuckets(const glm::vec3& cameraPos, RenderFrameData& out)
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

    // Sprites are painted in this order, so it doubles as the answer to which
    // sprite is on top: opaque pairs first (a blended quad drawn before an
    // opaque one would be overwritten by a pass that only cares about depth),
    // then the authored layer and sort order, both ascending so the bigger one
    // paints last -- the same pair the ray pick prefers at equal distance --
    // and finally far to near so the nearest quad blends last.
    const auto distanceToCamera2 = [&cameraPos](const WorldSpriteCandidate& sprite)
    {
        return glm::distance2(cameraPos, sprite.worldCenter);
    };
    std::sort(out.worldSprites.order.begin(), out.worldSprites.order.end(), [&](uint32_t lhs, uint32_t rhs)
              {
                  const auto& a = (*out.worldSprites.source)[lhs];
                  const auto& b = (*out.worldSprites.source)[rhs];
                  if (a.bTranslucent != b.bTranslucent) {
                      return !a.bTranslucent;
                  }
                  if (a.layer != b.layer) {
                      return a.layer < b.layer;
                  }
                  if (a.sortOrder != b.sortOrder) {
                      return a.sortOrder < b.sortOrder;
                  }
                  return distanceToCamera2(a) > distanceToCamera2(b);
              });
}

} // namespace ya
