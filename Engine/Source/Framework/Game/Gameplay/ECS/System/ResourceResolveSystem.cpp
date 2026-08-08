#include "ResourceResolveSystem.Detail.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "Host/App.h"
#include "Scene/Runtime/SceneManager.h"
#include "Render3D/Terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

namespace ya
{

namespace
{

float halfToFloat(uint16_t value)
{
    const uint32_t sign = (value & 0x8000u) << 16;
    uint32_t       exp  = (value & 0x7C00u) >> 10;
    uint32_t       mant = value & 0x03FFu;

    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        }
        else {
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x03FFu;
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1Fu) {
        bits = sign | 0x7F800000u | (mant << 13);
    }
    else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::vector<float> extractTerrainHeights(const AssetManager::TextureMemoryBlock& texture)
{
    std::vector<float> heights;
    if (!texture.isValid() || texture.channels == 0) {
        return heights;
    }

    const size_t pixelCount = static_cast<size_t>(texture.width) * texture.height;
    heights.resize(pixelCount, 0.0f);

    switch (texture.payloadType) {
    case AssetManager::ETexturePayloadType::U8: {
        const auto* data = texture.bytes.data();
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = static_cast<float>(data[i * texture.channels]) / 255.0f;
        }
        break;
    }
    case AssetManager::ETexturePayloadType::F16: {
        const auto* data = reinterpret_cast<const uint16_t*>(texture.bytes.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = halfToFloat(data[i * texture.channels]);
        }
        break;
    }
    case AssetManager::ETexturePayloadType::F32: {
        const auto* data = reinterpret_cast<const float*>(texture.bytes.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = data[i * texture.channels];
        }
        break;
    }
    default:
        heights.clear();
        break;
    }

    for (auto& height : heights) {
        height = std::clamp(height, 0.0f, 1.0f);
    }
    return heights;
}

std::string buildTerrainDerivedKey(const TerrainComponent& terrain, uint64_t heightMapVersion)
{
    return std::format("terrain|{}|{}|{:.6f}|{:.6f}|{:.6f}|{}|{}",
                       AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath()),
                       heightMapVersion,
                       terrain._size.x,
                       terrain._size.y,
                       terrain._heightScale,
                       terrain._heightOffset,
                       terrain._gridResolution);
}

void copySkyboxResourceToRuntime(const std::shared_ptr<SkyboxDerivedResource>& resource,
                                 SkyboxRuntimeState&                           state)
{
    state.boundResource        = resource;
    state.cubemapRenderImage   = resource ? resource->cubemapRenderImage : nullptr;
    state.cubemapTexture       = resource ? resource->cubemapTexture : nullptr;
    state.sourcePreviewTexture = resource ? resource->sourcePreviewTexture : nullptr;
    state.cubemapFacePreviewViews.fill(nullptr);
    if (!resource) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex] = resource->cubemapFacePreviewViews[faceIndex];
    }
}

void copyEnvironmentResourceToRuntime(const std::shared_ptr<EnvironmentLightingDerivedResource>& resource,
                                      EnvironmentLightingRuntimeState&                            state)
{
    state.boundResource        = resource;
    state.cubemapRenderImage   = resource ? resource->cubemapRenderImage : nullptr;
    state.cubemapTexture       = resource ? resource->cubemapTexture : nullptr;
    state.irradianceRenderImage = resource ? resource->irradianceRenderImage : nullptr;
    state.prefilterRenderImage  = resource ? resource->prefilterRenderImage : nullptr;
    state.cubemapFacePreviewViews.fill(nullptr);
    state.irradianceFacePreviewViews.fill(nullptr);
    for (auto& mipViews : state.prefilterMipFacePreviewViews) {
        mipViews.fill(nullptr);
    }
    state.prefilterPreviewMipCount = resource ? resource->prefilterPreviewMipCount : 0;

    if (!resource) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex]    = resource->cubemapFacePreviewViews[faceIndex];
        state.irradianceFacePreviewViews[faceIndex] = resource->irradianceFacePreviewViews[faceIndex];
    }
    for (uint32_t mipIndex = 0; mipIndex < resource->prefilterPreviewMipCount; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            state.prefilterMipFacePreviewViews[mipIndex][faceIndex] =
                resource->prefilterMipFacePreviewViews[mipIndex][faceIndex];
        }
    }
}

} // namespace

void ResourceResolveSystem::init()
{
    YA_CORE_ASSERT(_render, "ResourceResolveSystem requires render before init");
    _equidistantCylindrical2CubeMap.init(_render);
    _cubeMap2IrradianceMap.init(_render);
    _cubeMap2PrefilterPipeline.init(_render);
}

void ResourceResolveSystem::clearSceneResolveWork()
{
    for (auto& [entity, pendingState] : _terrainStates) {
        (void)entity;
        pendingState = TerrainRuntimeState{};
    }
    for (auto& [entity, pendingState] : _skyboxStates) {
        (void)entity;
        detail::resetSkyboxState(pendingState);
    }
    for (auto& [entity, pendingState] : _environmentStates) {
        (void)entity;
        detail::resetEnvState(pendingState);
    }

    _terrainStates.clear();
    _skyboxStates.clear();
    _environmentStates.clear();
    _dirtyTerrainQueue.clear();
    _dirtySkyboxQueue.clear();
    _dirtyEnvironmentQueue.clear();
    _dirtyMaterialQueue.clear();
    _dirtyTerrainSet.clear();
    _dirtySkyboxSet.clear();
    _dirtyEnvironmentSet.clear();
    _dirtyMaterialSet.clear();
    _activeTerrain.clear();
    _activeSkybox.clear();
    _activeEnvironment.clear();
    _activeMaterial.clear();
    _sceneSkyboxEnvironmentDependents.clear();
    _nextResolveAuditFrame = 0;
    _pendingStateScene = nullptr;
}

void ResourceResolveSystem::clearAllResolveState()
{
    clearSceneResolveWork();
    _terrainDerivedResources.clear();

    for (auto& [key, resource] : _skyboxDerivedResources) {
        (void)key;
        if (!resource) {
            continue;
        }
        SkyboxRuntimeState temp{};
        copySkyboxResourceToRuntime(resource, temp);
        detail::resetSkyboxState(temp);
    }
    _skyboxDerivedResources.clear();

    for (auto& [key, resource] : _environmentDerivedResources) {
        (void)key;
        if (!resource) {
            continue;
        }
        EnvironmentLightingRuntimeState temp{};
        copyEnvironmentResourceToRuntime(resource, temp);
        detail::resetEnvState(temp);
    }
    _environmentDerivedResources.clear();
}

void ResourceResolveSystem::clearPendingResolveStates()
{
    clearAllResolveState();
}

void ResourceResolveSystem::seedSceneResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        (void)terrain;
        markTerrainDirty(entity, "scene seed", terrain.getRebuildNotBeforeFrame());
    }
    for (auto&& [entity, skybox] : registry.view<SkyboxComponent>().each()) {
        (void)skybox;
        markSkyboxDirty(entity, "scene seed");
    }
    for (auto&& [entity, environment] : registry.view<EnvironmentLightingComponent>().each()) {
        if (environment.usesSceneSkybox()) {
            _sceneSkyboxEnvironmentDependents.insert(entity);
        }
        markEnvironmentLightingDirty(entity, "scene seed");
    }
    for (auto&& [entity, unused] : registry.view<PhongMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(entity, "scene seed");
    }
    for (auto&& [entity, unused] : registry.view<PBRMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(entity, "scene seed");
    }
    for (auto&& [entity, unused] : registry.view<UnlitMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(entity, "scene seed");
    }
}

bool ResourceResolveSystem::isTerrainQueuedOrActive(entt::entity entity) const
{
    return _dirtyTerrainSet.contains(entity) || _activeTerrain.contains(entity);
}

bool ResourceResolveSystem::isSkyboxQueuedOrActive(entt::entity entity) const
{
    return _dirtySkyboxSet.contains(entity) || _activeSkybox.contains(entity);
}

bool ResourceResolveSystem::isEnvironmentQueuedOrActive(entt::entity entity) const
{
    return _dirtyEnvironmentSet.contains(entity) || _activeEnvironment.contains(entity);
}

bool ResourceResolveSystem::isMaterialQueuedOrActive(entt::entity entity) const
{
    return _dirtyMaterialSet.contains(entity) || _activeMaterial.contains(entity);
}

void ResourceResolveSystem::auditResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    const uint64_t currentFrame = App::currentFrameIndex();
    if (_nextResolveAuditFrame != 0 && currentFrame < _nextResolveAuditFrame) {
        return;
    }
    _nextResolveAuditFrame = currentFrame + 120;

    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();

    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        auto& state = _terrainStates[entity];
        const bool bVersionNotCompleted = terrain.getAuthoringVersion() > state.lastCompletedAuthoringVersion;
        bool       bHeightMapStale      = false;
        if (assets && terrain.hasHeightMap() &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            bHeightMapStale = state.lastBuiltHeightMapVersion !=
                              assets->getResourceVersion(terrain._heightMapRef.getPath());
        }

        if ((bVersionNotCompleted || bHeightMapStale) && !isTerrainQueuedOrActive(entity)) {
            YA_CORE_WARN("ResourceResolve audit re-queued Terrain entity {}: completedVersion={}, authoringVersion={}, stale={}",
                         static_cast<uint32_t>(entity),
                         state.lastCompletedAuthoringVersion,
                         terrain.getAuthoringVersion(),
                         bHeightMapStale);
            markTerrainDirty(entity, bHeightMapStale ? "audit: height map stale" : "audit: missed terrain enqueue",
                             terrain.getRebuildNotBeforeFrame());
        }
    }

    for (auto&& [entity, skybox] : registry.view<SkyboxComponent>().each()) {
        auto& state = _skyboxStates[entity];
        if (skybox.authoringVersion > state.lastCompletedAuthoringVersion && !isSkyboxQueuedOrActive(entity)) {
            YA_CORE_WARN("ResourceResolve audit re-queued Skybox entity {}: completedVersion={}, authoringVersion={}",
                         static_cast<uint32_t>(entity),
                         state.lastCompletedAuthoringVersion,
                         skybox.authoringVersion);
            markSkyboxDirty(entity, "audit: missed skybox enqueue");
        }
    }

    for (auto&& [entity, environment] : registry.view<EnvironmentLightingComponent>().each()) {
        auto& state = _environmentStates[entity];
        if (environment.usesSceneSkybox()) {
            _sceneSkyboxEnvironmentDependents.insert(entity);
        }
        else {
            _sceneSkyboxEnvironmentDependents.erase(entity);
        }

        if (environment.authoringVersion > state.lastCompletedAuthoringVersion && !isEnvironmentQueuedOrActive(entity)) {
            YA_CORE_WARN("ResourceResolve audit re-queued EnvironmentLighting entity {}: completedVersion={}, authoringVersion={}",
                         static_cast<uint32_t>(entity),
                         state.lastCompletedAuthoringVersion,
                         environment.authoringVersion);
            markEnvironmentLightingDirty(entity, "audit: missed environment enqueue");
        }
    }

}

void ResourceResolveSystem::auditMaterialWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    const uint64_t currentFrame = App::currentFrameIndex();
    if (_nextMaterialAuditFrame != 0 && currentFrame < _nextMaterialAuditFrame) {
        return;
    }
    _nextMaterialAuditFrame = currentFrame + MATERIAL_AUDIT_INTERVAL_FRAMES;

    auto& registry = scene->getRegistry();

    const auto auditMaterial = [&](auto&& view) {
        for (auto&& [entity, material] : view.each()) {
            (void)material;
            // The per-frame needsResolve sweep should have queued every
            // component that needs work. A component still unqueued here
            // means a modification path bypassed the dirty queue — surface
            // it in dev builds, self-heal in release.
            if (material.needsResolve() && !isMaterialQueuedOrActive(entity)) {
                YA_CORE_ASSERT(false, "ResourceResolve audit: material needs resolve but was not queued");
                YA_CORE_WARN("ResourceResolve audit re-queued Material entity {}: missed enqueue",
                             static_cast<uint32_t>(entity));
                markMaterialDirty(entity, "audit: missed material enqueue");
            }
            // Texture staleness (hot reload) is only detected by the periodic
            // audit; mark dirty so the next pump re-resolves the component.
            if (material.isResolved() && material.checkTexturesStaleness()) {
                markMaterialDirty(entity, "audit: texture stale");
            }
        }
    };
    auditMaterial(registry.view<PhongMaterialComponent>());
    auditMaterial(registry.view<PBRMaterialComponent>());
    auditMaterial(registry.view<UnlitMaterialComponent>());
}

void ResourceResolveSystem::markAllSceneSkyboxEnvironmentDependentsDirty(const char* reason)
{
    const std::vector<entt::entity> dependents(_sceneSkyboxEnvironmentDependents.begin(),
                                               _sceneSkyboxEnvironmentDependents.end());
    for (const auto entity : dependents) {
        markEnvironmentLightingDirty(entity, reason);
    }
}

void ResourceResolveSystem::touchDerivedResourceUsage()
{
    const uint64_t currentFrame = App::currentFrameIndex();
    for (const auto& [entity, state] : _skyboxStates) {
        (void)entity;
        if (state.boundResource) {
            state.boundResource->lastUsedFrame = currentFrame;
        }
    }
    for (const auto& [entity, state] : _environmentStates) {
        (void)entity;
        if (state.boundResource) {
            state.boundResource->lastUsedFrame = currentFrame;
        }
    }
    for (const auto& [entity, state] : _terrainStates) {
        (void)entity;
        if (state.boundResource) {
            state.boundResource->lastUsedFrame = currentFrame;
        }
    }
}

void ResourceResolveSystem::gcDerivedResources(uint64_t currentFrame)
{
    const auto shouldKeep = [currentFrame](uint64_t lastUsedFrame) {
        return lastUsedFrame + DERIVED_RESOURCE_GC_DELAY_FRAMES > currentFrame;
    };

    for (auto it = _terrainDerivedResources.begin(); it != _terrainDerivedResources.end();) {
        if (!it->second || shouldKeep(it->second->lastUsedFrame)) {
            ++it;
            continue;
        }
        it = _terrainDerivedResources.erase(it);
    }

    for (auto it = _skyboxDerivedResources.begin(); it != _skyboxDerivedResources.end();) {
        if (!it->second || shouldKeep(it->second->lastUsedFrame)) {
            ++it;
            continue;
        }

        SkyboxRuntimeState temp{};
        copySkyboxResourceToRuntime(it->second, temp);
        detail::resetSkyboxState(temp);
        it = _skyboxDerivedResources.erase(it);
    }

    for (auto it = _environmentDerivedResources.begin(); it != _environmentDerivedResources.end();) {
        if (!it->second || shouldKeep(it->second->lastUsedFrame)) {
            ++it;
            continue;
        }

        EnvironmentLightingRuntimeState temp{};
        copyEnvironmentResourceToRuntime(it->second, temp);
        detail::resetEnvState(temp);
        it = _environmentDerivedResources.erase(it);
    }
}

void ResourceResolveSystem::cleanupTerrainState(entt::entity entity)
{
    _terrainStates.erase(entity);
    _dirtyTerrainSet.erase(entity);
    _activeTerrain.erase(entity);
    std::erase(_dirtyTerrainQueue, entity);
}

void ResourceResolveSystem::cleanupSkyboxState(entt::entity entity)
{
    if (auto it = _skyboxStates.find(entity); it != _skyboxStates.end()) {
        detail::resetSkyboxState(it->second);
        _skyboxStates.erase(it);
    }
    _dirtySkyboxSet.erase(entity);
    _activeSkybox.erase(entity);
    std::erase(_dirtySkyboxQueue, entity);
}

void ResourceResolveSystem::cleanupEnvironmentLightingState(entt::entity entity)
{
    if (auto it = _environmentStates.find(entity); it != _environmentStates.end()) {
        detail::resetEnvState(it->second);
        _environmentStates.erase(it);
    }
    _dirtyEnvironmentSet.erase(entity);
    _activeEnvironment.erase(entity);
    _sceneSkyboxEnvironmentDependents.erase(entity);
    std::erase(_dirtyEnvironmentQueue, entity);
}

void ResourceResolveSystem::cleanupMaterialState(entt::entity entity)
{
    _dirtyMaterialSet.erase(entity);
    _activeMaterial.erase(entity);
    std::erase(_dirtyMaterialQueue, entity);
}

void ResourceResolveSystem::markTerrainDirty(entt::entity entity, const char* reason, uint64_t rebuildNotBeforeFrame)
{
    if (!_pendingStateScene) {
        return;
    }

    auto& registry = _pendingStateScene->getRegistry();
    if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
        cleanupTerrainState(entity);
        return;
    }

    auto& terrain = registry.get<TerrainComponent>(entity);
    if (rebuildNotBeforeFrame > terrain.getRebuildNotBeforeFrame()) {
        terrain.setRebuildNotBeforeFrame(rebuildNotBeforeFrame);
    }

    auto& state = _terrainStates[entity];
    state.state                      = terrain.hasHeightMap() ? TerrainRuntimeState::EResolveState::Dirty
                                                              : TerrainRuntimeState::EResolveState::Empty;
    state.pendingHeightMapHandle     = 0;
    state.lastQueuedAuthoringVersion = terrain.getAuthoringVersion();
    state.lastDirtyReason            = reason ? reason : "dirty";
    if (_dirtyTerrainSet.insert(entity).second) {
        _dirtyTerrainQueue.push_back(entity);
    }
}

void ResourceResolveSystem::markSkyboxDirty(entt::entity entity, const char* reason)
{
    if (!_pendingStateScene) {
        return;
    }

    auto& registry = _pendingStateScene->getRegistry();
    if (!registry.valid(entity) || !registry.all_of<SkyboxComponent>(entity)) {
        cleanupSkyboxState(entity);
        return;
    }

    auto& skybox = registry.get<SkyboxComponent>(entity);
    auto& state  = _skyboxStates[entity];
    state.resolveState               = skybox.hasSource() ? ESkyboxResolveState::Dirty
                                                          : ESkyboxResolveState::Empty;
    state.lastQueuedAuthoringVersion = skybox.authoringVersion;
    state.lastDirtyReason            = reason ? reason : "dirty";
    if (_dirtySkyboxSet.insert(entity).second) {
        _dirtySkyboxQueue.push_back(entity);
    }
}

void ResourceResolveSystem::markEnvironmentLightingDirty(entt::entity entity, const char* reason)
{
    if (!_pendingStateScene) {
        return;
    }

    auto& registry = _pendingStateScene->getRegistry();
    if (!registry.valid(entity) || !registry.all_of<EnvironmentLightingComponent>(entity)) {
        cleanupEnvironmentLightingState(entity);
        return;
    }

    auto& environment = registry.get<EnvironmentLightingComponent>(entity);
    if (environment.usesSceneSkybox()) {
        _sceneSkyboxEnvironmentDependents.insert(entity);
    }
    else {
        _sceneSkyboxEnvironmentDependents.erase(entity);
    }

    auto& state = _environmentStates[entity];
    state.sourceState                = environment.hasSource()
                                           ? EEnvironmentLightingSourceResolveState::Dirty
                                           : EEnvironmentLightingSourceResolveState::Empty;
    state.irradianceState            = environment.bEnableIrradiance
                                           ? EEnvironmentLightingIrradianceResolveState::Dirty
                                           : EEnvironmentLightingIrradianceResolveState::Disabled;
    state.prefilterState             = environment.bEnablePrefilter
                                           ? EEnvironmentLightingPrefilterResolveState::Dirty
                                           : EEnvironmentLightingPrefilterResolveState::Disabled;
    state.lastQueuedAuthoringVersion = environment.authoringVersion;
    state.lastDirtyReason            = reason ? reason : "dirty";
    if (_dirtyEnvironmentSet.insert(entity).second) {
        _dirtyEnvironmentQueue.push_back(entity);
    }
}

void ResourceResolveSystem::markMaterialDirty(entt::entity entity, const char* reason)
{
    if (!_pendingStateScene) {
        return;
    }

    auto& registry = _pendingStateScene->getRegistry();
    if (!registry.valid(entity) ||
        (!registry.all_of<PhongMaterialComponent>(entity) &&
         !registry.all_of<PBRMaterialComponent>(entity) &&
         !registry.all_of<UnlitMaterialComponent>(entity))) {
        cleanupMaterialState(entity);
        return;
    }

    (void)reason;
    if (_dirtyMaterialSet.insert(entity).second) {
        _dirtyMaterialQueue.push_back(entity);
    }
}

void ResourceResolveSystem::shutdown()
{
    clearAllResolveState();
    _cubeMap2PrefilterPipeline.shutdown();
    _cubeMap2IrradianceMap.shutdown();
    _equidistantCylindrical2CubeMap.shutdown();
    _getActiveScene = {};
    _offscreenQueueService = {};
    _render = nullptr;
}

void ResourceResolveSystem::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();

    (void)dt;

    auto* const scene = _getActiveScene ? _getActiveScene() : nullptr;
    if (!scene) {
        clearSceneResolveWork();
        return;
    }

    if (_pendingStateScene != scene) {
        clearSceneResolveWork();
        _pendingStateScene = scene;
        seedSceneResolveWork(scene);
    }

    auditResolveWork(scene);
    gcDerivedResources(App::currentFrameIndex());

    {
        YA_PROFILE_SCOPE("ResourceResolve/Meshes");
        resolvePendingMeshes(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/Terrain");
        resolvePendingTerrain(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/Materials");
        resolvePendingMaterials(scene);
        // Periodic staleness / missed-enqueue audit runs after the per-frame
        // sweep so freshly modified components are already queued and only
        // true bypasses trip the dev assertion.
        auditMaterialWork(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/UI");
        resolvePendingUI(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/Billboards");
        resolvePendingBillboards(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/Skybox");
        resolvePendingSkybox(scene);
    }
    {
        YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting");
        resolvePendingEnvironmentLighting(scene);
    }
    touchDerivedResourceUsage();
}

Mesh* ResourceResolveSystem::getTerrainMesh(entt::entity entity) const
{
    const auto it = _terrainStates.find(entity);
    if (it == _terrainStates.end() || !it->second.boundResource) {
        return nullptr;
    }
    return it->second.boundResource->mesh.get();
}

const TerrainRuntimeState* ResourceResolveSystem::findTerrainState(entt::entity entity) const
{
    const auto it = _terrainStates.find(entity);
    return it == _terrainStates.end() ? nullptr : &it->second;
}

ESkyboxResolveState ResourceResolveSystem::getSkyboxResolveState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? ESkyboxResolveState::Empty : it->second.resolveState;
}

bool ResourceResolveSystem::isSkyboxLoading(entt::entity entity) const
{
    const auto state = getSkyboxResolveState(entity);
    return state == ESkyboxResolveState::ResolvingSource || state == ESkyboxResolveState::Preprocessing;
}

const SkyboxRuntimeState* ResourceResolveSystem::findSkyboxState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? nullptr : &it->second;
}


void ResourceResolveSystem::resolvePendingMeshes(Scene* scene)
{
    auto& registry = scene->getRegistry();

    auto resolveOne = [](auto& meshComp) {
        if (!meshComp.isResolved() && meshComp.hasMeshSource()) {
            meshComp.resolve();
        }
    };

    registry.view<StaticMeshComponent>().each([&](auto entity, StaticMeshComponent& comp) {
        (void)entity;
        resolveOne(comp);
    });
    registry.view<SkinnedMeshComponent>().each([&](auto entity, SkinnedMeshComponent& comp) {
        (void)entity;
        resolveOne(comp);
    });
}

void ResourceResolveSystem::resolvePendingTerrain(Scene* scene)
{
    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();
    if (!assets) {
        return;
    }

    auto pumpOne = [&](entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
            cleanupTerrainState(entity);
            return;
        }

        auto& terrain = registry.get<TerrainComponent>(entity);
        auto& state   = _terrainStates[entity];
        const uint64_t currentFrame = App::currentFrameIndex();

        if (terrain.getRebuildNotBeforeFrame() > currentFrame) {
            _activeTerrain.insert(entity);
            return;
        }

        if (!terrain.hasHeightMap()) {
            state.state = TerrainRuntimeState::EResolveState::Empty;
            state.pendingHeightMapHandle = 0;
            state.lastBuiltHeightMapVersion = 0;
            state.currentDerivedKey.clear();
            state.boundResource.reset();
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        const uint64_t heightMapVersion = assets->getResourceVersion(terrain._heightMapRef.getPath());
        const std::string derivedKey = buildTerrainDerivedKey(terrain, heightMapVersion);
        if (!state.currentDerivedKey.empty() &&
            state.currentDerivedKey != derivedKey &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (auto it = _terrainDerivedResources.find(derivedKey); it != _terrainDerivedResources.end() &&
            it->second && it->second->mesh) {
            it->second->lastUsedFrame = currentFrame;
            state.currentDerivedKey   = derivedKey;
            state.boundResource       = it->second;
            state.lastBuiltHeightMapVersion = it->second->heightMapVersion;
            state.pendingHeightMapHandle    = 0;
            state.state                     = TerrainRuntimeState::EResolveState::Ready;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        if (state.state == TerrainRuntimeState::EResolveState::Ready &&
            state.lastBuiltHeightMapVersion != heightMapVersion) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (state.state != TerrainRuntimeState::EResolveState::Dirty &&
            state.state != TerrainRuntimeState::EResolveState::LoadingHeightMap) {
            state.currentDerivedKey = derivedKey;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        if (state.pendingHeightMapHandle == 0) {
            const auto handle = assets->loadTextureBatchIntoMemory(AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths   = {terrain._heightMapRef.getPath()},
                .colorSpace  = AssetManager::ETextureColorSpace::Linear,
            });
            state.pendingHeightMapHandle = handle;
            state.state                  = TerrainRuntimeState::EResolveState::LoadingHeightMap;
            state.lastStartedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.insert(entity);
            return;
        }

        AssetManager::TextureBatchMemory batchMemory;
        if (!assets->consumeTextureBatchMemory(state.pendingHeightMapHandle, batchMemory)) {
            _activeTerrain.insert(entity);
            return;
        }
        state.pendingHeightMapHandle = 0;

        if (!batchMemory.isValid() || batchMemory.textures.empty()) {
            YA_CORE_WARN("Terrain height map decode failed: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        const auto& texture = batchMemory.textures.front();
        if (AssetManager::normalizeAssetPath(texture.filepath) != AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath())) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
            markTerrainDirty(entity, "terrain stale async result", terrain.getRebuildNotBeforeFrame());
            return;
        }

        auto heights = extractTerrainHeights(texture);
        if (heights.empty()) {
            YA_CORE_WARN("Terrain height map has unsupported payload: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        auto meshData = buildTerrainMeshData(TerrainMeshBuildDesc{
            .name           = std::format("terrain_{}", terrain._heightMapRef.getPath()),
            .size           = terrain._size,
            .heightScale    = terrain._heightScale,
            .heightOffset   = terrain._heightOffset,
            .gridResolution = terrain._gridResolution,
            .heightWidth    = texture.width,
            .heightHeight   = texture.height,
            .heights        = heights,
        });

        auto resource            = std::make_shared<TerrainDerivedResource>();
        auto* render = getRender();
        YA_CORE_ASSERT(render, "ResourceResolveSystem terrain mesh creation requires render backend");
        resource->mesh           = Mesh::create(*render, meshData);
        resource->heightMapVersion = heightMapVersion;
        resource->lastUsedFrame  = currentFrame;
        _terrainDerivedResources[derivedKey] = resource;

        state.currentDerivedKey  = derivedKey;
        state.boundResource      = resource;
        state.pendingHeightMapHandle   = 0;
        state.lastBuiltHeightMapVersion = heightMapVersion;
        state.state                    = TerrainRuntimeState::EResolveState::Ready;
        state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
        _activeTerrain.erase(entity);
    };

    while (!_dirtyTerrainQueue.empty()) {
        const auto entity = _dirtyTerrainQueue.front();
        _dirtyTerrainQueue.pop_front();
        _dirtyTerrainSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeTerrain.begin(), _activeTerrain.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

void ResourceResolveSystem::resolvePendingMaterials(Scene* scene)
{
    auto& registry = scene->getRegistry();

    // Per-frame O(1) sweep: components that were just created or modified
    // (constructor / invalidate / reflection setter set the Dirty state
    // without notifying the resolver) are enqueued here so they resolve on
    // the next frame. No string normalization or staleness work happens in
    // this sweep — that stays in the periodic audit.
    const auto sweepNeedsResolve = [&](auto&& view) {
        for (auto&& [entity, material] : view.each()) {
            (void)material;
            if (material.needsResolve() && !isMaterialQueuedOrActive(entity)) {
                markMaterialDirty(entity, "needs-resolve sweep");
            }
        }
    };
    sweepNeedsResolve(registry.view<PhongMaterialComponent>());
    sweepNeedsResolve(registry.view<PBRMaterialComponent>());
    sweepNeedsResolve(registry.view<UnlitMaterialComponent>());

    auto pumpOne = [&](entt::entity entity) {
        if (!registry.valid(entity)) {
            cleanupMaterialState(entity);
            return;
        }

        auto pumpComponent = [&](auto& materialComponent) {
            if (materialComponent.needsResolve()) {
                materialComponent.resolve();
            }
            else if (materialComponent.isResolved()) {
                materialComponent.checkTexturesStaleness();
            }
        };

        bool bHandled = false;
        if (auto* phong = registry.try_get<PhongMaterialComponent>(entity)) {
            pumpComponent(*phong);
            bHandled = true;
        }
        if (auto* pbr = registry.try_get<PBRMaterialComponent>(entity)) {
            pumpComponent(*pbr);
            bHandled = true;
        }
        if (auto* unlit = registry.try_get<UnlitMaterialComponent>(entity)) {
            pumpComponent(*unlit);
            bHandled = true;
        }
        if (!bHandled) {
            cleanupMaterialState(entity);
            return;
        }

        // A component stuck in the async Resolving state must keep being
        // pumped every frame until its textures arrive.
        const bool bStillResolving =
            (registry.all_of<PhongMaterialComponent>(entity) &&
             registry.get<PhongMaterialComponent>(entity).needsResolve()) ||
            (registry.all_of<PBRMaterialComponent>(entity) &&
             registry.get<PBRMaterialComponent>(entity).needsResolve()) ||
            (registry.all_of<UnlitMaterialComponent>(entity) &&
             registry.get<UnlitMaterialComponent>(entity).needsResolve());
        if (bStillResolving) {
            _activeMaterial.insert(entity);
        }
        else {
            _activeMaterial.erase(entity);
        }
    };

    while (!_dirtyMaterialQueue.empty()) {
        const auto entity = _dirtyMaterialQueue.front();
        _dirtyMaterialQueue.pop_front();
        _dirtyMaterialSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeMaterial.begin(), _activeMaterial.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

void ResourceResolveSystem::resolvePendingUI(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<UIComponent>().each([&](auto entity, UIComponent& uiComponent) {
        (void)entity;
        if (!uiComponent.view.textureRef.isLoaded() && uiComponent.view.textureRef.hasPath()) {
            uiComponent.view.textureRef.resolve();
        }
    });
}

void ResourceResolveSystem::resolvePendingBillboards(Scene* scene)
{
    auto& registry = scene->getRegistry();

    for (const auto& [entity, comp] : registry.view<BillboardComponent>().each()) {
        (void)entity;
        if (comp.bDirty) {
            comp.resolve();
        }
    }
}


const SkyboxRuntimeState* ResourceResolveSystem::findFirstSceneSkyboxState(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
        const auto* state = findSkyboxState(entity);
        if (state && state->resolveState == ESkyboxResolveState::Ready && state && state->hasRenderableCubemap()) {
            return state;
        }
    }

    return nullptr;
}

EEnvironmentLightingSourceResolveState ResourceResolveSystem::getEnvironmentSourceState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingSourceResolveState::Empty : it->second.sourceState;
}

EEnvironmentLightingIrradianceResolveState ResourceResolveSystem::getEnvironmentIrradianceState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingIrradianceResolveState::Empty : it->second.irradianceState;
}

EEnvironmentLightingPrefilterResolveState ResourceResolveSystem::getEnvironmentPrefilterState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingPrefilterResolveState::Empty : it->second.prefilterState;
}

bool ResourceResolveSystem::isEnvironmentLightingLoading(entt::entity entity) const
{
    return getEnvironmentSourceState(entity) == EEnvironmentLightingSourceResolveState::ResolvingSource ||
           getEnvironmentSourceState(entity) == EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap ||
           getEnvironmentIrradianceState(entity) == EEnvironmentLightingIrradianceResolveState::Building ||
           getEnvironmentPrefilterState(entity) == EEnvironmentLightingPrefilterResolveState::Building;
}

const EnvironmentLightingRuntimeState* ResourceResolveSystem::findEnvironmentLightingState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? nullptr : &it->second;
}

const EnvironmentLightingRuntimeState* ResourceResolveSystem::findFirstSceneEnvironmentLightingState(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (state && state->irradianceState == EEnvironmentLightingIrradianceResolveState::Ready && state->hasIrradianceMap()) {
            return state;
        }
    }

    return nullptr;
}

ImageResourceRef ResourceResolveSystem::resolveSceneSkyboxResource(Scene* scene) const
{
    ImageResourceRef resource{};
    const auto* state = findFirstSceneSkyboxState(scene);
    if (!state) {
        return resource;
    }

    resource.renderImage = state->cubemapRenderImage;
    resource.texture     = state->cubemapTexture;
    return resource;
}

EnvironmentLightingSceneResources ResourceResolveSystem::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    EnvironmentLightingSceneResources resources{};
    if (!scene) {
        return resources;
    }

    const auto* skyboxState = findFirstSceneSkyboxState(scene);
    if (skyboxState) {
        resources.cubemap.renderImage = skyboxState->cubemapRenderImage;
        resources.cubemap.texture     = skyboxState->cubemapTexture;
    }

    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (!state) {
            continue;
        }

        if (state && state->sourceState == EEnvironmentLightingSourceResolveState::Ready) {
            if (elc.usesSceneSkybox()) {
                resources.cubemap.renderImage = skyboxState ? skyboxState->cubemapRenderImage : nullptr;
                resources.cubemap.texture     = skyboxState ? skyboxState->cubemapTexture : nullptr;
            }
            else if (state->hasRenderableCubemap()) {
                resources.cubemap.renderImage = state->cubemapRenderImage;
                resources.cubemap.texture     = state->cubemapTexture;
            }
        }

        if (!resources.irradiance.isValid() && state && state->irradianceState == EEnvironmentLightingIrradianceResolveState::Ready && state->hasIrradianceMap()) {
            resources.irradiance.renderImage = state->irradianceRenderImage;
        }

        if (!resources.prefilter.isValid() && state && state->prefilterState == EEnvironmentLightingPrefilterResolveState::Ready && state->hasPrefilterMap()) {
            resources.prefilter.renderImage = state->prefilterRenderImage;
        }

        if (resources.cubemap.isValid() && resources.irradiance.isValid() && resources.prefilter.isValid()) {
            break;
        }
    }

    return resources;
}

SkyboxPreviewInfo ResourceResolveSystem::getSkyboxPreview(entt::entity entity) const
{
    SkyboxPreviewInfo info{};

    const auto* state = findSkyboxState(entity);
    if (!state) {
        return info;
    }

    info.sourcePreviewTexture  = state->sourcePreviewTexture.get();
    info.cubemapRenderImage    = state->cubemapRenderImage;
    info.cubemapImage          = detail::getImageShared(state->cubemapRenderImage, state->cubemapTexture);
    info.bHasRenderableCubemap = state->hasRenderableCubemap();

    for (uint32_t index = 0; index < CubeFace_Count; ++index) {
        info.cubemapFaceViews[index] = state->cubemapFacePreviewViews[index].get();
    }

    return info;
}

EnvironmentLightingPreviewInfo ResourceResolveSystem::getEnvironmentLightingPreview(entt::entity entity) const
{
    EnvironmentLightingPreviewInfo info{};

    const auto* state = findEnvironmentLightingState(entity);
    if (!state) {
        return info;
    }

    const EnvironmentLightingComponent* component = nullptr;
    const SkyboxRuntimeState*           sceneSkyboxState = nullptr;
    if (_pendingStateScene) {
        auto& registry = _pendingStateScene->getRegistry();
        if (registry.valid(entity) && registry.all_of<EnvironmentLightingComponent>(entity)) {
            component = &registry.get<EnvironmentLightingComponent>(entity);
            if (component->usesSceneSkybox()) {
                sceneSkyboxState = findFirstSceneSkyboxState(_pendingStateScene);
            }
        }
    }

    const bool bUseSceneSkyboxSource = component && component->usesSceneSkybox() &&
                                       sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap();
    const auto cubemapRenderImage = bUseSceneSkyboxSource ? sceneSkyboxState->cubemapRenderImage : state->cubemapRenderImage;
    const auto cubemapImage = bUseSceneSkyboxSource
        ? detail::getImageShared(sceneSkyboxState->cubemapRenderImage, sceneSkyboxState->cubemapTexture)
        : detail::getImageShared(state->cubemapRenderImage, state->cubemapTexture);

    info.cubemapRenderImage    = cubemapRenderImage;
    info.cubemapImage          = cubemapImage;
    info.irradianceRenderImage = state->irradianceRenderImage;
    info.irradianceImage       = state->irradianceRenderImage ? state->irradianceRenderImage->getImageShared() : nullptr;
    info.prefilterRenderImage  = state->prefilterRenderImage;
    info.prefilterImage        = state->prefilterRenderImage ? state->prefilterRenderImage->getImageShared() : nullptr;
    info.prefilterMipCount     = state->prefilterPreviewMipCount;
    info.bHasRenderableCubemap = bUseSceneSkyboxSource ? sceneSkyboxState->hasRenderableCubemap() : state->hasRenderableCubemap();
    info.bHasIrradianceMap     = state->hasIrradianceMap();
    info.bHasPrefilterMap      = state->hasPrefilterMap();

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        info.cubemapFaceViews[faceIndex]    = bUseSceneSkyboxSource
            ? sceneSkyboxState->cubemapFacePreviewViews[faceIndex].get()
            : state->cubemapFacePreviewViews[faceIndex].get();
        info.irradianceFaceViews[faceIndex] = state->irradianceFacePreviewViews[faceIndex].get();
    }

    for (uint32_t mipIndex = 0; mipIndex < state->prefilterPreviewMipCount; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            info.prefilterMipFaceViews[mipIndex][faceIndex] = state->prefilterMipFacePreviewViews[mipIndex][faceIndex].get();
        }
    }

    return info;
}

} // namespace ya
