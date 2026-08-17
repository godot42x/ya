#include "EnvironmentLightingProcessor.h"
#include "EnvironmentLightingDetail.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "RHI/Render.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Core/OffscreenJob.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

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


void EnvironmentLightingProcessor::init()
{
    YA_CORE_ASSERT(_render, "EnvironmentLightingProcessor requires render before init");
    _equidistantCylindrical2CubeMap.init(_render);
    _cubeMap2IrradianceMap.init(_render);
    _cubeMap2PrefilterPipeline.init(_render);
}

void EnvironmentLightingProcessor::clearAllResolveState()
{
    clearSceneResolveWork();

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

void EnvironmentLightingProcessor::clearPendingResolveStates()
{
    clearAllResolveState();
}

void EnvironmentLightingProcessor::onUpdate(float dt)
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
    gcDerivedResources(_getFrameIndex ? _getFrameIndex() : 0);

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

void EnvironmentLightingProcessor::clearSceneResolveWork()
{
    for (auto& [entity, pendingState] : _skyboxStates) {
        (void)entity;
        detail::resetSkyboxState(pendingState);
    }
    for (auto& [entity, pendingState] : _environmentStates) {
        (void)entity;
        detail::resetEnvState(pendingState);
    }

    _skyboxStates.clear();
    _environmentStates.clear();
    _dirtySkyboxQueue.clear();
    _dirtyEnvironmentQueue.clear();
    _dirtySkyboxSet.clear();
    _dirtyEnvironmentSet.clear();
    _activeSkybox.clear();
    _activeEnvironment.clear();
    _sceneSkyboxEnvironmentDependents.clear();
    _nextResolveAuditFrame = 0;
    _pendingStateScene = nullptr;
}

void EnvironmentLightingProcessor::seedSceneResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
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
}

bool EnvironmentLightingProcessor::isSkyboxQueuedOrActive(entt::entity entity) const
{
    return _dirtySkyboxSet.contains(entity) || _activeSkybox.contains(entity);
}

bool EnvironmentLightingProcessor::isEnvironmentQueuedOrActive(entt::entity entity) const
{
    return _dirtyEnvironmentSet.contains(entity) || _activeEnvironment.contains(entity);
}

void EnvironmentLightingProcessor::auditResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    const uint64_t currentFrame = _getFrameIndex ? _getFrameIndex() : 0;
    if (_nextResolveAuditFrame != 0 && currentFrame < _nextResolveAuditFrame) {
        return;
    }
    _nextResolveAuditFrame = currentFrame + 120;

    auto& registry = scene->getRegistry();
    // auto* assets   = AssetManager::get();

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

void EnvironmentLightingProcessor::markAllSceneSkyboxEnvironmentDependentsDirty(const char* reason)
{
    const std::vector<entt::entity> dependents(_sceneSkyboxEnvironmentDependents.begin(),
                                               _sceneSkyboxEnvironmentDependents.end());
    for (const auto entity : dependents) {
        markEnvironmentLightingDirty(entity, reason);
    }
}

void EnvironmentLightingProcessor::touchDerivedResourceUsage()
{
    const uint64_t currentFrame = _getFrameIndex ? _getFrameIndex() : 0;
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
}

void EnvironmentLightingProcessor::gcDerivedResources(uint64_t currentFrame)
{
    const auto shouldKeep = [currentFrame](uint64_t lastUsedFrame) {
        return lastUsedFrame + DERIVED_RESOURCE_GC_DELAY_FRAMES > currentFrame;
    };

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

void EnvironmentLightingProcessor::cleanupSkyboxState(entt::entity entity)
{
    if (auto it = _skyboxStates.find(entity); it != _skyboxStates.end()) {
        detail::resetSkyboxState(it->second);
        _skyboxStates.erase(it);
    }
    _dirtySkyboxSet.erase(entity);
    _activeSkybox.erase(entity);
    std::erase(_dirtySkyboxQueue, entity);
}

void EnvironmentLightingProcessor::cleanupEnvironmentLightingState(entt::entity entity)
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

void EnvironmentLightingProcessor::markSkyboxDirty(entt::entity entity, const char* reason)
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

void EnvironmentLightingProcessor::markEnvironmentLightingDirty(entt::entity entity, const char* reason)
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

void EnvironmentLightingProcessor::shutdown()
{
    clearAllResolveState();
    _cubeMap2PrefilterPipeline.shutdown();
    _cubeMap2IrradianceMap.shutdown();
    _equidistantCylindrical2CubeMap.shutdown();
    _getActiveScene = {};
    _offscreenQueueService = {};
    _render = nullptr;
}

ESkyboxResolveState EnvironmentLightingProcessor::getSkyboxResolveState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? ESkyboxResolveState::Empty : it->second.resolveState;
}

bool EnvironmentLightingProcessor::isSkyboxLoading(entt::entity entity) const
{
    const auto state = getSkyboxResolveState(entity);
    return state == ESkyboxResolveState::ResolvingSource || state == ESkyboxResolveState::Preprocessing;
}

const SkyboxRuntimeState* EnvironmentLightingProcessor::findSkyboxState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? nullptr : &it->second;
}


EEnvironmentLightingSourceResolveState EnvironmentLightingProcessor::getEnvironmentSourceState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingSourceResolveState::Empty : it->second.sourceState;
}

const SkyboxRuntimeState* EnvironmentLightingProcessor::findFirstSceneSkyboxState(Scene* scene) const
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

EEnvironmentLightingIrradianceResolveState EnvironmentLightingProcessor::getEnvironmentIrradianceState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingIrradianceResolveState::Empty : it->second.irradianceState;
}

EEnvironmentLightingPrefilterResolveState EnvironmentLightingProcessor::getEnvironmentPrefilterState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? EEnvironmentLightingPrefilterResolveState::Empty : it->second.prefilterState;
}

bool EnvironmentLightingProcessor::isEnvironmentLightingLoading(entt::entity entity) const
{
    return getEnvironmentSourceState(entity) == EEnvironmentLightingSourceResolveState::ResolvingSource ||
           getEnvironmentSourceState(entity) == EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap ||
           getEnvironmentIrradianceState(entity) == EEnvironmentLightingIrradianceResolveState::Building ||
           getEnvironmentPrefilterState(entity) == EEnvironmentLightingPrefilterResolveState::Building;
}

const EnvironmentLightingRuntimeState* EnvironmentLightingProcessor::findEnvironmentLightingState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? nullptr : &it->second;
}

const EnvironmentLightingRuntimeState* EnvironmentLightingProcessor::findFirstSceneEnvironmentLightingState(Scene* scene) const
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

std::shared_ptr<ImageResource> EnvironmentLightingProcessor::resolveSceneSkyboxResource(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    if (!state) {
        return nullptr;
    }

    return detail::ownerResourceOf(state->cubemapRenderImage, state->cubemapTexture);
}

EnvironmentLightingSceneResources EnvironmentLightingProcessor::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    EnvironmentLightingSceneResources resources{};
    if (!scene) {
        return resources;
    }

    const auto* skyboxState = findFirstSceneSkyboxState(scene);
    if (skyboxState) {
        resources.cubemap = detail::ownerResourceOf(skyboxState->cubemapRenderImage, skyboxState->cubemapTexture);
    }

    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (!state) {
            continue;
        }

        if (state && state->sourceState == EEnvironmentLightingSourceResolveState::Ready) {
            if (elc.usesSceneSkybox()) {
                resources.cubemap = skyboxState ? detail::ownerResourceOf(skyboxState->cubemapRenderImage, skyboxState->cubemapTexture) : nullptr;
            }
            else if (state->hasRenderableCubemap()) {
                resources.cubemap = detail::ownerResourceOf(state->cubemapRenderImage, state->cubemapTexture);
            }
        }

        if (!resources.irradiance && state && state->irradianceState == EEnvironmentLightingIrradianceResolveState::Ready && state->hasIrradianceMap()) {
            resources.irradiance = state->irradianceRenderImage ? state->irradianceRenderImage->getResourceShared() : nullptr;
        }

        if (!resources.prefilter && state && state->prefilterState == EEnvironmentLightingPrefilterResolveState::Ready && state->hasPrefilterMap()) {
            resources.prefilter = state->prefilterRenderImage ? state->prefilterRenderImage->getResourceShared() : nullptr;
        }

        if (resources.cubemap && resources.irradiance && resources.prefilter) {
            break;
        }
    }

    return resources;
}

SkyboxPreviewInfo EnvironmentLightingProcessor::getSkyboxPreview(entt::entity entity) const
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

EnvironmentLightingPreviewInfo EnvironmentLightingProcessor::getEnvironmentLightingPreview(entt::entity entity) const
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


#include "RHI/Render.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Core/OffscreenJob.h"


#include <algorithm>
#include <format>

namespace ya::detail
{

void retireTexture(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), std::move(texture));
    texture = nullptr;
}

void retireTextureNow(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    DeferredDeletionQueue::get().retireResource(texture);
    texture.reset();
}

std::shared_ptr<RenderTexture> adoptRenderTexture(const std::shared_ptr<ImageResource>& image)
{
    if (!image) {
        return nullptr;
    }

    return RenderTexture::adopt(image, image->getLabel());
}

void retireRenderTexture(std::shared_ptr<RenderTexture>& texture)
{
    if (!texture) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), std::move(texture));
    texture = nullptr;
}

void retireRenderTextureNow(std::shared_ptr<RenderTexture>& texture)
{
    if (!texture) {
        return;
    }

    DeferredDeletionQueue::get().retireResource(texture);
    texture.reset();
}

std::shared_ptr<ImageResource> ownerResourceOf(const std::shared_ptr<RenderTexture>& renderImage, const stdptr<Texture>& texture)
{
    if (renderImage && renderImage->getResourceShared()) {
        return renderImage->getResourceShared();
    }

    return texture ? texture->getResourceShared() : nullptr;
}

std::shared_ptr<IImage> getImageShared(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture)
{
    if (texture && texture->getImageShared()) {
        return texture->getImageShared();
    }

    return fallbackTexture ? fallbackTexture->getImageShared() : nullptr;
}

IImageView* getImageView(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture)
{
    if (texture && texture->getImageView()) {
        return texture->getImageView();
    }

    return fallbackTexture ? fallbackTexture->getImageView() : nullptr;
}

std::shared_ptr<IImageView> getImageViewShared(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture)
{
    if (texture && texture->getImageViewShared()) {
        return texture->getImageViewShared();
    }

    return fallbackTexture ? fallbackTexture->getImageViewShared() : nullptr;
}

EFormat::T chooseSkyboxCubemapFormat(EFormat::T sourceFormat)
{
    switch (sourceFormat) {
    case EFormat::R8G8B8A8_SRGB:
        return EFormat::R8G8B8A8_SRGB;
    case EFormat::R16G16B16A16_SFLOAT:
        return EFormat::R16G16B16A16_SFLOAT;
    default:
        return EFormat::R8G8B8A8_UNORM;
    }
}

// Always use R16G16B16A16_SFLOAT for irradiance maps regardless of source format,
// because irradiance convolution accumulates many low-intensity samples and needs
// the extra precision to avoid banding artifacts.
EFormat::T chooseEnvironmentIrradianceFormat(EFormat::T /*sourceFormat*/)
{
    return EFormat::R16G16B16A16_SFLOAT;
}

// Equirectangular maps have 2:1 aspect ratio; each cube face covers 1/4 width × 1/2 height.
uint32_t computeSkyboxFaceSize(const Texture* sourceTexture)
{
    if (!sourceTexture) {
        return 0;
    }

    const auto width  = sourceTexture->getWidth();
    const auto height = sourceTexture->getHeight();
    if (width == 0 || height == 0) {
        return 0;
    }

    return std::max(1u, std::min(width / 4u, height / 2u));
}

uint32_t computeEnvironmentIrradianceFaceSize(const Texture* sourceTexture, uint32_t requestedFaceSize)
{
    if (!sourceTexture) {
        return 0;
    }

    const uint32_t maxFaceSize    = std::max(4u, requestedFaceSize);
    const uint32_t sourceFaceSize = std::max(1u, std::min(sourceTexture->getWidth(), sourceTexture->getHeight()));
    return std::max(4u, std::min(sourceFaceSize, maxFaceSize));
}

std::shared_ptr<ImageResource> createRenderableSkyboxResource(IRender*           render,
                                                              const std::string& label,
                                                              uint32_t           faceSize,
                                                              EFormat::T         format,
                                                              int                mips)
{
    auto* resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory || faceSize == 0 || format == EFormat::Undefined) {
        return nullptr;
    }

    ImageCreateInfo ci{
        .label  = std::format("{}_Image", label),
        .format = format,
        .extent = {
            .width  = faceSize,
            .height = faceSize,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = CubeFace_Count,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled),
        .initialLayout = EImageLayout::Undefined,
        .flags         = EImageCreateFlag::CubeCompatible,
    };
    if (mips > 0) {
        ci.mipLevels = mips;
        ci.usage     = static_cast<EImageUsage::T>(ci.usage | EImageUsage::TransferDst | EImageUsage::TransferSrc);
    }

    auto image = resourceFactory->createImage(ci);
    if (!image) {
        return nullptr;
    }

    auto cubeView = resourceFactory->createImageView(
        image,
        ImageViewCreateInfo{
            .label       = std::format("{}_CubeView", label),
            .viewType    = EImageViewType::ViewCube,
            .aspectFlags = EImageAspect::Color,
            .baseMipLevel = 0,
            .levelCount   = ci.mipLevels,
            .baseArrayLayer = 0,
            .layerCount     = CubeFace_Count,
        });
    if (!cubeView) {
        return nullptr;
    }

    auto renderImage       = std::make_shared<ImageResource>();
    renderImage->label     = label;
    renderImage->image     = std::move(image);
    renderImage->defaultView = std::move(cubeView);
    return renderImage;
}

std::shared_ptr<RenderTexture> createRenderableSkyboxCubemap(IRender*           render,
                                                             const std::string& label,
                                                             uint32_t           faceSize,
                                                             EFormat::T         format,
                                                             int                mips)
{
    return adoptRenderTexture(createRenderableSkyboxResource(render, label, faceSize, format, mips));
}

OffscreenJobState::CreateOutputFn makeCubemapOutputFn(const std::string& label,
                                                      uint32_t           faceSize,
                                                      EFormat::T         format,
                                                      int                mipLevels)
{
    return [label, faceSize, format, mipLevels](IRender* render) -> std::shared_ptr<ImageResource>
    {
        if (!render || label.empty() || faceSize == 0 || format == EFormat::Undefined || mipLevels <= 0) {
            return nullptr;
        }

        return createRenderableSkyboxResource(render, label, faceSize, format, mipLevels);
    };
}

void tryQueueJob(const OffscreenJobQueueService& queueService, IRender* render, const std::shared_ptr<OffscreenJobState>& job)
{
    if (!job || !job->isReadyToQueue()) {
        return;
    }

    queueOffscreenJob(queueService, render, job);
}

} // namespace ya::detail


#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Core/OffscreenJob.h"
#include "Scene/Core/Scene.h"

#include <format>
#include <vector>

namespace ya
{

namespace
{

void clearSkyboxViews(SkyboxRuntimeState& state)
{
    for (auto& faceView : state.cubemapFacePreviewViews) {
        if (!faceView) {
            continue;
        }

        DeferredDeletionQueue::get().retireResource(std::move(faceView));
    }
}

std::string buildSkyboxDerivedKey(const SkyboxComponent& skybox, AssetManager* assets)
{
    if (!assets || !skybox.hasSource()) {
        return {};
    }

    if (skybox.hasCubemapSource()) {
        std::string key = std::format("skybox|cubefaces|flip={}", skybox.cubemapSource.flipVertical ? 1 : 0);
        for (const auto& path : skybox.cubemapSource.files) {
            const auto normalized = AssetManager::normalizeAssetPath(path);
            key += std::format("|{}|v{}", normalized, assets->getResourceVersion(normalized));
        }
        return key;
    }

    const auto normalized = AssetManager::normalizeAssetPath(skybox.cylindricalSource.filepath);
    return std::format("skybox|cyl|{}|v{}|flip={}",
                       normalized,
                       assets->getResourceVersion(normalized),
                       skybox.cylindricalSource.flipVertical ? 1 : 0);
}

void applySkyboxResource(const std::shared_ptr<SkyboxDerivedResource>& resource,
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

std::shared_ptr<SkyboxDerivedResource> snapshotSkyboxResource(const SkyboxRuntimeState& state, uint64_t currentFrame)
{
    auto resource                    = std::make_shared<SkyboxDerivedResource>();
    resource->cubemapRenderImage     = state.cubemapRenderImage;
    resource->cubemapTexture         = state.cubemapTexture;
    resource->sourcePreviewTexture   = state.sourcePreviewTexture;
    resource->cubemapFacePreviewViews = state.cubemapFacePreviewViews;
    resource->lastUsedFrame          = currentFrame;
    return resource;
}

} // namespace

namespace detail
{

void rebuildSkyboxViews(IRender* render, SkyboxRuntimeState& state)
{
    clearSkyboxViews(state);
    const auto cubemapImage = getImageShared(state.cubemapRenderImage, state.cubemapTexture);
    if (!cubemapImage || !getImageView(state.cubemapRenderImage, state.cubemapTexture)) {
        return;
    }

    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex] = resourceFactory->createImageView(
            cubemapImage,
            ImageViewCreateInfo{
                .label          = std::format("SkyboxPreviewFace{}", faceIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = faceIndex,
                .layerCount     = 1,
            });
    }
}

void retireSkyboxResources(SkyboxRuntimeState& state)
{
    retireTexture(state.cubemapTexture);
    retireRenderTexture(state.cubemapRenderImage);
    retireTexture(state.sourcePreviewTexture);
    clearSkyboxViews(state);
}

void resetSkyboxPending(SkyboxRuntimeState& state)
{
    state.pendingBatchLoadState.reset();
    state.pendingCylindricalFuture.reset();
    cancelOffscreenJob(state.pendingOffscreenProcess);
}

void resetSkyboxState(SkyboxRuntimeState& state)
{
    resetSkyboxPending(state);
    retireSkyboxResources(state);
    state.resultVersion = 0;
}

} // namespace detail

void EnvironmentLightingProcessor::resolvePendingSkybox(Scene* scene)
{
    YA_PROFILE_FUNCTION();
    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();

    auto pumpOne = [&](entt::entity entity) {
        YA_PROFILE_SCOPE("ResourceResolve/Skybox/Entity");
        if (!registry.valid(entity) || !registry.all_of<SkyboxComponent>(entity)) {
            cleanupSkyboxState(entity);
            return;
        }

        auto& sc           = registry.get<SkyboxComponent>(entity);
        auto& pendingState = _skyboxStates[entity];
        const auto previousResultVersion = pendingState.resultVersion;
        const std::string derivedKey = buildSkyboxDerivedKey(sc, assets);
        // clear invalid version
        if (pendingState.authoringVersion != sc.authoringVersion) {
            detail::resetSkyboxState(pendingState);
            pendingState.authoringVersion = sc.authoringVersion;
            pendingState.lastStartedAuthoringVersion = sc.authoringVersion;
        }

        if (!sc.hasSource()) {
            if (pendingState.resolveState != ESkyboxResolveState::Empty) {
                makeTransition(pendingState.resolveState, "Skybox")
                    .to(ESkyboxResolveState::Empty, "no source");
                detail::resetSkyboxState(pendingState);
            }
            pendingState.derivedKey.clear();
            pendingState.boundResource.reset();
            pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
            _activeSkybox.erase(entity);
            return;
        }

        if (!derivedKey.empty() && pendingState.derivedKey != derivedKey &&
            pendingState.resultVersion > 0) {
            detail::resetSkyboxState(pendingState);
            pendingState.resultVersion = 0;
            makeTransition(pendingState.resolveState, "Skybox")
                .to(ESkyboxResolveState::Dirty, "derived key changed");
        }

        if (!derivedKey.empty()) {
            if (auto it = _skyboxDerivedResources.find(derivedKey); it != _skyboxDerivedResources.end() &&
                it->second && it->second->hasRenderableCubemap()) {
                const bool bCacheRebound = pendingState.resultVersion == 0 ||
                                           pendingState.derivedKey != derivedKey ||
                                           pendingState.boundResource.get() != it->second.get() ||
                                           pendingState.resolveState != ESkyboxResolveState::Ready;
                it->second->lastUsedFrame = _getFrameIndex ? _getFrameIndex() : 0;
                applySkyboxResource(it->second, pendingState);
                pendingState.derivedKey = derivedKey;
                if (bCacheRebound) {
                    makeTransition(pendingState.resolveState, "Skybox")
                        .to(ESkyboxResolveState::Ready, "derived cache hit");
                    ++pendingState.resultVersion;
                    markAllSceneSkyboxEnvironmentDependentsDirty("scene skybox projection rebound");
                }
                pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
                _activeSkybox.erase(entity);
                return;
            }
        }

        if (pendingState.resolveState == ESkyboxResolveState::Dirty ||
            pendingState.resolveState == ESkyboxResolveState::Empty) {
            detail::resetSkyboxPending(pendingState);
            pendingState.lastStartedAuthoringVersion = sc.authoringVersion;
        }

        auto transition = makeTransition(pendingState.resolveState, "Skybox");
        switch (pendingState.resolveState) {
        case ESkyboxResolveState::Dirty:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/Dirty");
            if (sc.hasCubemapSource()) {
                std::vector<std::string> facePaths(sc.cubemapSource.files.begin(),
                                                   sc.cubemapSource.files.end());
                pendingState.pendingBatchLoadState =
                    std::make_shared<SkyboxPendingBatchLoadState>();
                pendingState.pendingBatchLoadState->batchHandle =
                    AssetManager::get()->loadTextureBatchIntoMemory(
                        AssetManager::TextureBatchMemoryLoadRequest{
                            .filepaths = facePaths,
                        });
            }
            else if (sc.hasCylindricalSource()) {
                pendingState.pendingCylindricalFuture =
                    AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                        .filepath        = sc.cylindricalSource.filepath,
                        .name            = "SkyboxCylindricalSource",
                        .onReady         = {},
                        .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                        .textureSemantic = std::nullopt,
                    });
            }
            transition.to(ESkyboxResolveState::ResolvingSource,
                          "source load requested");
        } break;

        case ESkyboxResolveState::ResolvingSource:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/ResolvingSource");
            if (sc.hasCubemapSource()) {
                AssetManager::TextureBatchMemory batchMemory;

                if (!pendingState.pendingBatchLoadState) {
                    break;
                }
                if (!AssetManager::get()->consumeTextureBatchMemory(
                        pendingState.pendingBatchLoadState->batchHandle, batchMemory)) {
                    break;
                }

                pendingState.pendingBatchLoadState.reset();
                if (batchMemory.textures.size() != CubeFace_Count ||
                    !batchMemory.isValid()) {
                    detail::retireSkyboxResources(pendingState);
                    transition.fail("cubemap batch invalid");
                    break;
                }

                CubeMapMemoryCreateInfo createInfo;
                createInfo.label        = "SkyboxCubemap";
                createInfo.flipVertical = sc.cubemapSource.flipVertical;

                for (size_t index = 0; index < CubeFace_Count; ++index) {
                    const auto& face        = batchMemory.textures[index];
                    createInfo.faces[index] = TextureMemoryView{
                        .width    = face.width,
                        .height   = face.height,
                        .channels = face.channels,
                        .format   = face.format,
                        .data     = face.bytes.data(),
                        .dataSize = face.bytes.size(),
                    };
                }

                auto* render = getRender();
                auto cubemap = render ? Texture::createCubeMapFromMemory(*render, createInfo) : nullptr;
                if (!cubemap || !cubemap->isValid()) {
                    detail::retireSkyboxResources(pendingState);
                    transition.fail("cubemap creation failed");
                    break;
                }

                pendingState.cubemapTexture = std::move(cubemap);
                pendingState.cubemapRenderImage.reset();
                detail::rebuildSkyboxViews(getRender(), pendingState);
                pendingState.derivedKey = derivedKey;
                ++pendingState.resultVersion;
                auto resource = snapshotSkyboxResource(pendingState, _getFrameIndex ? _getFrameIndex() : 0);
                _skyboxDerivedResources[derivedKey] = resource;
                pendingState.boundResource = resource;
                transition.to(ESkyboxResolveState::Ready, "cubemap source resolved");
                break;
            }
            else if (sc.hasCylindricalSource()) {
                if (!pendingState.pendingCylindricalFuture.has_value() ||
                    !pendingState.pendingCylindricalFuture->isReady()) {
                    pendingState.pendingCylindricalFuture =
                        AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                            .filepath        = sc.cylindricalSource.filepath,
                            .name            = "SkyboxCylindricalSource",
                            .onReady         = {},
                            .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                            .textureSemantic = std::nullopt,
                        });
                }
                if (!pendingState.pendingCylindricalFuture.has_value() ||
                    !pendingState.pendingCylindricalFuture->isReady()) {
                    break;
                }

                auto sourceTexture = pendingState.pendingCylindricalFuture->getShared();
                pendingState.pendingCylindricalFuture.reset();
                if (!sourceTexture || !sourceTexture->getImageView()) {
                    transition.fail("cylindrical source invalid");
                    break;
                }

                pendingState.sourcePreviewTexture = sourceTexture;

                // do the convert job
                auto job       = ya::makeShared<OffscreenJobState>();
                job->debugName = std::format("SkyboxCubemap_{}", static_cast<uint32_t>(entity));
                auto jobResult = job->result;
                job->executeFn = [&pipeline = getCylindrical2CubePipeline(),
                                  src       = sourceTexture,
                                  flipV     = sc.cylindricalSource.flipVertical,
                                  jobResult](
                                     ICommandBuffer* cmdBuf, ImageResource* output) -> bool
                {
                    auto result = pipeline.execute({
                        .cmdBuf        = cmdBuf,
                        .input         = src.get(),
                        .output        = output,
                        .bFlipVertical = flipV,
                    });
                    if (jobResult && !result.keepAliveResources.empty()) {
                        auto& retained = jobResult->retainedResources;
                        retained.insert(retained.end(),
                                        std::make_move_iterator(result.keepAliveResources.begin()),
                                        std::make_move_iterator(result.keepAliveResources.end()));
                    }
                    if (result.transientOutputArrayView) {
                        DeferredDeletionQueue::get().retireResource(
                            result.transientOutputArrayView);
                    }
                    return result.bSuccess;
                };
                job->createOutputFn = detail::makeCubemapOutputFn(
                    job->debugName, detail::computeSkyboxFaceSize(sourceTexture.get()), detail::chooseSkyboxCubemapFormat(sourceTexture->getFormat()));

                pendingState.pendingOffscreenProcess = std::move(job);
                transition.to(ESkyboxResolveState::Preprocessing,
                              "queue cylindrical preprocess");
                break;
            }

            detail::resetSkyboxPending(pendingState);
            transition.to(ESkyboxResolveState::Empty,
                          "active source changed while resolving");
        } break;

        case ESkyboxResolveState::Preprocessing:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/Preprocessing");
            if (!pendingState.pendingOffscreenProcess) {
                transition.fail("preprocess job missing");
                break;
            }

            if (pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Pending) {
                detail::tryQueueJob(getOffscreenJobQueueService(), getRender(), pendingState.pendingOffscreenProcess);
                break;
            }

            if (pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Queued ||
                pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Recorded) {
                break;
            }

            if (pendingState.pendingOffscreenProcess->hasFailed() ||
                !pendingState.pendingOffscreenProcess->result ||
                !pendingState.pendingOffscreenProcess->result->outputImage) {
                pendingState.pendingOffscreenProcess.reset();
                detail::retireSkyboxResources(pendingState);
                transition.fail("preprocess failed");
                break;
            }

            if (!pendingState.pendingOffscreenProcess->isGpuCompleted()) {
                break;
            }

            pendingState.cubemapRenderImage = detail::adoptRenderTexture(pendingState.pendingOffscreenProcess->result->outputImage);
            detail::retireTextureNow(pendingState.cubemapTexture);
            pendingState.pendingOffscreenProcess.reset();
            detail::rebuildSkyboxViews(getRender(), pendingState);
            pendingState.derivedKey = derivedKey;
            ++pendingState.resultVersion;
            auto resource = snapshotSkyboxResource(pendingState, _getFrameIndex ? _getFrameIndex() : 0);
            _skyboxDerivedResources[derivedKey] = resource;
            pendingState.boundResource = resource;
            makeTransition(pendingState.resolveState, "Skybox")
                .to(ESkyboxResolveState::Ready, "preprocess completed");
        } break;

        case ESkyboxResolveState::Empty:
        case ESkyboxResolveState::Ready:
        case ESkyboxResolveState::Failed:
        default:
            break;
        }

        const bool bActive = pendingState.resolveState == ESkyboxResolveState::ResolvingSource ||
                             pendingState.resolveState == ESkyboxResolveState::Preprocessing;
        if (bActive) {
            _activeSkybox.insert(entity);
        }
        else {
            pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
            _activeSkybox.erase(entity);
        }

        if (pendingState.resultVersion != previousResultVersion) {
            markAllSceneSkyboxEnvironmentDependentsDirty("scene skybox result changed");
        }
    };

    while (!_dirtySkyboxQueue.empty()) {
        const auto entity = _dirtySkyboxQueue.front();
        _dirtySkyboxQueue.pop_front();
        _dirtySkyboxSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeSkybox.begin(), _activeSkybox.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

} // namespace ya


#include "RHI/Render.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Core/OffscreenJob.h"
#include "Scene/Core/Scene.h"


#include <format>
#include <vector>

namespace ya
{

namespace
{


std::shared_ptr<OffscreenJobState> createEnvironmentCubemapJob(EnvironmentLightingProcessor&              system,
                                                               entt::entity                        entity,
                                                               const EnvironmentLightingComponent& component,
                                                               const stdptr<Texture>&              sourceTexture);
std::shared_ptr<OffscreenJobState> createEnvironmentIrradianceJob(EnvironmentLightingProcessor&              system,
                                                                  entt::entity                        entity,
                                                                  const EnvironmentLightingComponent& component,
                                                                  const std::shared_ptr<ImageResource>& sourceCubemap);
std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(EnvironmentLightingProcessor&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const std::shared_ptr<ImageResource>& sourceCubemap);



uint32_t computeEnvironmentPrefilterFaceSize(const Texture* sourceCubemap)
{
    static constexpr uint32_t MAX_PREFILTER_FACE_SIZE = 64;

    if (!sourceCubemap) {
        return 0;
    }

    return std::max(1u,
                    std::min({sourceCubemap->getWidth(),
                              sourceCubemap->getHeight(),
                              MAX_PREFILTER_FACE_SIZE}));
}

uint32_t computeEnvironmentPrefilterMipLevels(uint32_t faceSize)
{
    uint32_t mipLevels = 1;
    while (faceSize > 1) {
        faceSize = std::max(1u, faceSize / 2u);
        ++mipLevels;
    }
    return mipLevels;
}

EFormat::T chooseEnvironmentPrefilterFormat(EFormat::T sourceFormat)
{
    (void)sourceFormat;
    return EFormat::R16G16B16A16_SFLOAT;
}

std::string buildEnvironmentDerivedKey(const EnvironmentLightingComponent& component,
                                      AssetManager*                       assets,
                                      const SkyboxRuntimeState*          sceneSkyboxState)
{
    const auto baseSuffix = std::format("|irr={}|pref={}|irrsize={}",
                                        component.bEnableIrradiance ? 1 : 0,
                                        component.bEnablePrefilter ? 1 : 0,
                                        component.getResolvedIrradianceFaceSize());

    if (component.usesSceneSkybox()) {
        if (!sceneSkyboxState || !sceneSkyboxState->hasRenderableCubemap() || sceneSkyboxState->derivedKey.empty()) {
            return {};
        }
        return std::format("env|scene-skybox|{}{}", sceneSkyboxState->derivedKey, baseSuffix);
    }

    if (!assets || !component.hasSource()) {
        return {};
    }

    if (component.hasCubemapSource()) {
        std::string key = std::format("env|cubefaces|flip={}", component.cubemapSource.flipVertical ? 1 : 0);
        for (const auto& path : component.cubemapSource.files) {
            const auto normalized = AssetManager::normalizeAssetPath(path);
            key += std::format("|{}|v{}", normalized, assets->getResourceVersion(normalized));
        }
        key += baseSuffix;
        return key;
    }

    const auto normalized = AssetManager::normalizeAssetPath(component.cylindricalSource.filepath);
    return std::format("env|cyl|{}|v{}|flip={}{}",
                       normalized,
                       assets->getResourceVersion(normalized),
                       component.cylindricalSource.flipVertical ? 1 : 0,
                       baseSuffix);
}

void applyEnvironmentResource(const std::shared_ptr<EnvironmentLightingDerivedResource>& resource,
                              EnvironmentLightingRuntimeState&                            state)
{
    state.boundResource         = resource;
    state.cubemapRenderImage    = resource ? resource->cubemapRenderImage : nullptr;
    state.cubemapTexture        = resource ? resource->cubemapTexture : nullptr;
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

std::shared_ptr<EnvironmentLightingDerivedResource> snapshotEnvironmentResource(const EnvironmentLightingRuntimeState& state, uint64_t currentFrame)
{
    auto resource                         = std::make_shared<EnvironmentLightingDerivedResource>();
    resource->cubemapRenderImage         = state.cubemapRenderImage;
    resource->cubemapTexture             = state.cubemapTexture;
    resource->cubemapFacePreviewViews    = state.cubemapFacePreviewViews;
    resource->irradianceRenderImage      = state.irradianceRenderImage;
    resource->irradianceFacePreviewViews = state.irradianceFacePreviewViews;
    resource->prefilterRenderImage       = state.prefilterRenderImage;
    resource->prefilterMipFacePreviewViews = state.prefilterMipFacePreviewViews;
    resource->prefilterPreviewMipCount   = state.prefilterPreviewMipCount;
    resource->lastUsedFrame              = currentFrame;
    return resource;
}

bool isEnvironmentResolveComplete(const EnvironmentLightingComponent& component,
                                  const EnvironmentLightingRuntimeState& state)
{
    return state.sourceState == EEnvironmentLightingSourceResolveState::Ready &&
           (!component.bEnableIrradiance || state.irradianceState == EEnvironmentLightingIrradianceResolveState::Ready) &&
           (!component.bEnablePrefilter || state.prefilterState == EEnvironmentLightingPrefilterResolveState::Ready);
}

bool canUseEnvironmentDerivedResource(const EnvironmentLightingComponent& component,
                                      const EnvironmentLightingDerivedResource& resource)
{
    return resource.hasRenderableCubemap() &&
           (!component.bEnableIrradiance || resource.hasIrradianceMap()) &&
           (!component.bEnablePrefilter || resource.hasPrefilterMap());
}

void completeEnvironmentSource(IRender*                         render,
                               EnvironmentLightingComponent&    component,
                               EnvironmentLightingRuntimeState& state,
                               const char*                      reason)
{
    detail::rebuildEnvironmentCubemapViews(render, state);
    makeTransition(state.sourceState, "EnvironmentLighting.Source")
        .to(EEnvironmentLightingSourceResolveState::Ready, reason);
    ++state.resultVersion;

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
    }
    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
    }
}

void completeEnvironmentSourceFromDependency(EnvironmentLightingComponent&    component,
                                             EnvironmentLightingRuntimeState& state,
                                             const SkyboxRuntimeState*       sceneSkyboxState,
                                             const char*                      reason)
{
    if (sceneSkyboxState) {
        state.cubemapRenderImage = sceneSkyboxState->cubemapRenderImage;
        state.cubemapTexture     = sceneSkyboxState->cubemapTexture;
    }
    makeTransition(state.sourceState, "EnvironmentLighting.Source")
        .to(EEnvironmentLightingSourceResolveState::Ready, reason);
    ++state.resultVersion;

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
    }
    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
    }
}

[[nodiscard]] std::shared_ptr<ImageResource> resolveEnvironmentSourceCubemap(const EnvironmentLightingComponent&    component,
                                                                             const EnvironmentLightingRuntimeState& state,
                                                                             const SkyboxRuntimeState*             sceneSkyboxState)
{
    if (component.usesSceneSkybox()) {
        return sceneSkyboxState ? detail::ownerResourceOf(sceneSkyboxState->cubemapRenderImage, sceneSkyboxState->cubemapTexture) : nullptr;
    }

    return detail::ownerResourceOf(state.cubemapRenderImage, state.cubemapTexture);
}

void syncEnvironmentDerivedBranchEnablement(IRender*                         render,
                                            EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnableIrradiance) {
        cancelOffscreenJob(state.pendingIrradianceOffscreen);
        detail::retireRenderTextureNow(state.irradianceRenderImage);
        detail::rebuildEnvironmentIrradianceViews(render, state);
        if (state.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
            makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
                .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        }
    }
    else if (state.sourceState == EEnvironmentLightingSourceResolveState::Ready && state.irradianceState == EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Dirty, "irradiance enabled");
    }

    if (!component.bEnablePrefilter) {
        cancelOffscreenJob(state.pendingPrefilterOffscreen);
        detail::retireRenderTextureNow(state.prefilterRenderImage);
        detail::rebuildPrefilterViews(render, state);
        if (state.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
            makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
                .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        }
    }
    else if (state.sourceState == EEnvironmentLightingSourceResolveState::Ready && state.prefilterState == EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Dirty, "prefilter enabled");
    }
}

void failEnvironmentDerivedBranches(EnvironmentLightingComponent& component, EnvironmentLightingRuntimeState& state, const char* reason)
{
    if (component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail(reason);
    }
    else {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, reason);
    }

    if (component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail(reason);
    }
    else {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, reason);
    }
}

std::shared_ptr<OffscreenJobState> createEnvironmentCubemapJob(EnvironmentLightingProcessor&              system,
                                                               entt::entity                        entity,
                                                               const EnvironmentLightingComponent& component,
                                                               const stdptr<Texture>&              sourceTexture)
{
    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentCubemap_{}", static_cast<uint32_t>(entity));
    auto jobResult = job->result;

    // TODO(user): this is the single cubemap-preprocess job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCylindrical2CubePipeline(),
                      src       = sourceTexture,
                      flipV     = component.cylindricalSource.flipVertical,
                      jobResult](ICommandBuffer* cmdBuf, ImageResource* output) -> bool
    {
        auto result = pipeline.execute({
            .cmdBuf        = cmdBuf,
            .input         = src.get(),
            .output        = output,
            .bFlipVertical = flipV,
        });
        if (jobResult && !result.keepAliveResources.empty()) {
            auto& retained = jobResult->retainedResources;
            retained.insert(retained.end(),
                            std::make_move_iterator(result.keepAliveResources.begin()),
                            std::make_move_iterator(result.keepAliveResources.end()));
        }
        if (result.transientOutputArrayView) {
            DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(job->debugName,
                                                      detail::computeSkyboxFaceSize(sourceTexture.get()),
                                                      detail::chooseSkyboxCubemapFormat(sourceTexture->getFormat()));
    return job;
}

std::shared_ptr<OffscreenJobState> createEnvironmentIrradianceJob(EnvironmentLightingProcessor&              system,
                                                                  entt::entity                        entity,
                                                                  const EnvironmentLightingComponent& component,
                                                                  const std::shared_ptr<ImageResource>& sourceCubemap)
{
    const auto sourceImageShared = sourceCubemap ? sourceCubemap->getImageShared() : nullptr;
    const uint32_t sourceWidth = sourceImageShared ? sourceImageShared->getWidth() : 0;
    const auto sourceFormat = sourceImageShared ? sourceImageShared->getFormat() : EFormat::Undefined;
    if (sourceWidth == 0) {
        return nullptr;
    }
    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentIrradiance_{}", static_cast<uint32_t>(entity));
    auto jobResult = job->result;

    // TODO(user): this is the single irradiance job hook. Replace or extend executeFn here.
    job->executeFn = [srcCubemap = sourceCubemap, &system, jobResult](ICommandBuffer* cmdBuf, ImageResource* output) -> bool
    {
        auto result =
            system
                .getCube2IrradiancePipeline()
                .execute({
                    .cmdBuf       = cmdBuf,
                    .input        = srcCubemap.get(),
                    .output       = output,
                });
        if (jobResult && !result.keepAliveResources.empty()) {
            auto& retained = jobResult->retainedResources;
            retained.insert(retained.end(),
                            std::make_move_iterator(result.keepAliveResources.begin()),
                            std::make_move_iterator(result.keepAliveResources.end()));
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        std::max(4u, std::min(sourceWidth, std::max(4u, component.getResolvedIrradianceFaceSize()))),
        detail::chooseEnvironmentIrradianceFormat(sourceFormat));
    return job;
}

std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(EnvironmentLightingProcessor&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const std::shared_ptr<ImageResource>& sourceCubemap)
{
    (void)component;
    const auto     sourceImageShared = sourceCubemap ? sourceCubemap->getImageShared() : nullptr;
    const uint32_t sourceWidth  = sourceImageShared ? sourceImageShared->getWidth() : 0;
    const uint32_t sourceHeight = sourceImageShared ? sourceImageShared->getHeight() : 0;
    const auto     sourceFormat = sourceImageShared ? sourceImageShared->getFormat() : EFormat::Undefined;
    if (sourceWidth == 0 || sourceHeight == 0) {
        return nullptr;
    }

    const uint32_t faceSize  = std::max(1u, std::min({sourceWidth, sourceHeight, 64u}));
    const uint32_t mipLevels = computeEnvironmentPrefilterMipLevels(faceSize);

    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentPrefilter_{}", static_cast<uint32_t>(entity));
    auto jobResult = job->result;

    // TODO(user): this is the single prefilter job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCube2PrefilterPipeline(),
                      srcCubemap = sourceCubemap,
                      jobResult](ICommandBuffer* cmdBuf, ImageResource* output) -> bool
    {
        auto result = pipeline.execute({
            .cmdBuf       = cmdBuf,
            .input        = srcCubemap.get(),
            .output       = output,
        });
        if (jobResult && !result.keepAliveResources.empty()) {
            auto& retained = jobResult->retainedResources;
            retained.insert(retained.end(),
                            std::make_move_iterator(result.keepAliveResources.begin()),
                            std::make_move_iterator(result.keepAliveResources.end()));
        }
        if (result.transientOutputArrayView) {
            DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        faceSize,
        chooseEnvironmentPrefilterFormat(sourceFormat),
        static_cast<int>(mipLevels));
    return job;
}


void tryBeginEnvIrradianceJob(EnvironmentLightingProcessor&           system,
                              entt::entity                     entity,
                              EnvironmentLightingComponent&    component,
                              EnvironmentLightingRuntimeState& state,
                              const std::shared_ptr<ImageResource>& sourceCubemap)
{
    if (!sourceCubemap || !sourceCubemap->isValid()) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance source invalid");
        return;
    }

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        return;
    }

    detail::retireRenderTextureNow(state.irradianceRenderImage);

    auto job = createEnvironmentIrradianceJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance job not wired");
        return;
    }

    state.pendingIrradianceOffscreen = std::move(job);
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Building, "queue irradiance preprocess");
}

void tryBeginEnvPrefilterJob(EnvironmentLightingProcessor&           system,
                             entt::entity                     entity,
                             EnvironmentLightingComponent&    component,
                             EnvironmentLightingRuntimeState& state,
                             const std::shared_ptr<ImageResource>& sourceCubemap)
{
    if (!sourceCubemap || !sourceCubemap->isValid()) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter source invalid");
        return;
    }

    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        return;
    }

    detail::retireRenderTextureNow(state.prefilterRenderImage);

    auto job = createEnvironmentPrefilterJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter job not wired");
        return;
    }

    state.pendingPrefilterOffscreen = std::move(job);
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Building, "queue prefilter preprocess");
}

const SkyboxRuntimeState* syncEnvSkybox(EnvironmentLightingComponent&    component,
                                        EnvironmentLightingRuntimeState& state,
                                        const SkyboxRuntimeState*        sceneSkyboxState)
{
    if (!component.usesSceneSkybox()) {
        state.bSceneSkyboxDependencyReady = false;
        return nullptr;
    }

    const bool     bDependencyReady      = sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap();
    const uint64_t currentResultVersion  = bDependencyReady ? sceneSkyboxState->resultVersion : 0;
    const bool     bDependencyChanged    = state.lastSceneSkyboxResultVersion != currentResultVersion ||
                                           state.bSceneSkyboxDependencyReady != bDependencyReady;
    if (bDependencyChanged) {
        detail::resetEnvState(state);
        state.lastSceneSkyboxResultVersion = currentResultVersion;
        state.bSceneSkyboxDependencyReady  = bDependencyReady;
        const auto nextSourceState         = bDependencyReady ? EEnvironmentLightingSourceResolveState::Dirty
                                                              : EEnvironmentLightingSourceResolveState::Empty;
        makeTransition(state.sourceState, "EnvironmentLighting.Source")
            .to(nextSourceState, "scene skybox dependency changed");
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance
                    ? (bDependencyReady ? EEnvironmentLightingIrradianceResolveState::Dirty
                                        : EEnvironmentLightingIrradianceResolveState::Empty)
                    : EEnvironmentLightingIrradianceResolveState::Disabled,
                "scene skybox dependency changed");
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter
                    ? (bDependencyReady ? EEnvironmentLightingPrefilterResolveState::Dirty
                                        : EEnvironmentLightingPrefilterResolveState::Empty)
                    : EEnvironmentLightingPrefilterResolveState::Disabled,
                "scene skybox dependency changed");
    }

    if (bDependencyReady && state.sourceState == EEnvironmentLightingSourceResolveState::Dirty) {
        return sceneSkyboxState;
    }

    return nullptr;
}
} // namespace

namespace detail
{

namespace
{

template <typename TViews>
void clearCubeFaceViews(TViews& views)
{
    for (auto& faceView : views) {
        if (!faceView) {
            continue;
        }

        DeferredDeletionQueue::get().retireResource(std::move(faceView));
    }
}

void clearPrefilterViews(EnvironmentLightingRuntimeState& state)
{
    for (auto& mipViews : state.prefilterMipFacePreviewViews) {
        clearCubeFaceViews(mipViews);
    }
    state.prefilterPreviewMipCount = 0;
}

void rebuildCubeFaceViews(IRender*                                         render,
                          const stdptr<Texture>&                          texture,
                          const std::shared_ptr<RenderTexture>&           renderImage,
                          std::array<stdptr<IImageView>, CubeFace_Count>& outViews,
                          const std::string&                              labelPrefix)
{
    clearCubeFaceViews(outViews);
    const auto image = getImageShared(renderImage, texture);
    if (!image || !getImageView(renderImage, texture)) {
        return;
    }

    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        outViews[faceIndex] = resourceFactory->createImageView(
            image,
            ImageViewCreateInfo{
                .label          = std::format("{}_Face_{}", labelPrefix, faceIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = faceIndex,
                .layerCount     = 1,
            });
    }
}

} // namespace

void rebuildEnvironmentCubemapViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(render, state.cubemapTexture, state.cubemapRenderImage, state.cubemapFacePreviewViews, "EnvironmentCubemap");
}

void rebuildEnvironmentIrradianceViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(render, nullptr, state.irradianceRenderImage, state.irradianceFacePreviewViews, "EnvironmentIrradiance");
}

void rebuildPrefilterViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    clearPrefilterViews(state);
    const auto prefilterImage = state.prefilterRenderImage ? state.prefilterRenderImage->getImageShared() : nullptr;
    if (!prefilterImage || !state.prefilterRenderImage || !state.prefilterRenderImage->getImageView()) {
        return;
    }

    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    const uint32_t mipLevels       = std::min(prefilterImage->getMipLevels(), EnvironmentLightingRuntimeState::MAX_PREFILTER_PREVIEW_MIPS);
    state.prefilterPreviewMipCount = mipLevels;

    for (uint32_t mipIndex = 0; mipIndex < mipLevels; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            state.prefilterMipFacePreviewViews[mipIndex][faceIndex] = resourceFactory->createImageView(
                prefilterImage,
                ImageViewCreateInfo{
                    .label          = std::format("EnvironmentPrefilter_Mip_{}_Face_{}", mipIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Color,
                    .baseMipLevel   = mipIndex,
                    .levelCount     = 1,
                    .baseArrayLayer = faceIndex,
                    .layerCount     = 1,
                });
        }
    }
}

void retireEnvTextures(EnvironmentLightingRuntimeState& state)
{
    clearCubeFaceViews(state.cubemapFacePreviewViews);
    clearCubeFaceViews(state.irradianceFacePreviewViews);
    clearPrefilterViews(state);
    retireTexture(state.cubemapTexture);
    retireRenderTexture(state.cubemapRenderImage);
    retireRenderTexture(state.irradianceRenderImage);
    retireRenderTexture(state.prefilterRenderImage);
}

void resetEnvPending(EnvironmentLightingRuntimeState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    cancelOffscreenJob(state.pendingEnvironmentOffscreen);
    cancelOffscreenJob(state.pendingIrradianceOffscreen);
    cancelOffscreenJob(state.pendingPrefilterOffscreen);
}

void resetEnvState(EnvironmentLightingRuntimeState& state)
{
    resetEnvPending(state);
    retireEnvTextures(state);
    state.resultVersion = 0;
    state.lastSceneSkyboxResultVersion = 0;
    state.bSceneSkyboxDependencyReady  = false;
}

} // namespace detail

namespace
{

void handleEnvironmentNoSource(EnvironmentLightingComponent&    component,
                               EnvironmentLightingRuntimeState& state)
{
    if (state.sourceState != EEnvironmentLightingSourceResolveState::Empty) {
        makeTransition(state.sourceState, "EnvironmentLighting.Source")
            .to(EEnvironmentLightingSourceResolveState::Empty, "no source");
    }
    if (state.irradianceState != EEnvironmentLightingIrradianceResolveState::Empty &&
        state.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                            : EEnvironmentLightingIrradianceResolveState::Disabled,
                "no source");
    }
    if (state.prefilterState != EEnvironmentLightingPrefilterResolveState::Empty &&
        state.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                           : EEnvironmentLightingPrefilterResolveState::Disabled,
                "no source");
    }
    detail::resetEnvState(state);
}

void handleEnvironmentSourceDirty(EnvironmentLightingComponent&    component,
                                  EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (component.hasCubemapSource()) {
        std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
        state.pendingBatchLoad              = std::make_shared<EnvironmentLightingPendingBatchLoadState>();
        state.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
            AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths  = facePaths,
                .colorSpace = AssetManager::ETextureColorSpace::Linear,
            });
        transition.to(EEnvironmentLightingSourceResolveState::ResolvingSource, "source load requested");
        return;
    }

    if (component.hasCylindricalSource()) {
        state.pendingCylindricalFuture = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
            .filepath        = component.cylindricalSource.filepath,
            .name            = "EnvironmentLightingCylindricalSource",
            .onReady         = {},
            .colorSpace      = AssetManager::ETextureColorSpace::Linear,
            .textureSemantic = std::nullopt,
        });
        transition.to(EEnvironmentLightingSourceResolveState::ResolvingSource, "source load requested");
        return;
    }

    transition.to(EEnvironmentLightingSourceResolveState::Empty, "source removed while dirty");
}

void handleEnvironmentSourceResolving(EnvironmentLightingProcessor&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!state.pendingBatchLoad || !AssetManager::get()->consumeTextureBatchMemory(state.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        state.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap batch invalid");
            failEnvironmentDerivedBranches(component, state, "cubemap batch invalid");
            return;
        }

        CubeMapMemoryCreateInfo createInfo;
        createInfo.label        = "EnvironmentLightingCubemap";
        createInfo.flipVertical = component.cubemapSource.flipVertical;

        for (size_t index = 0; index < CubeFace_Count; ++index) {
            const auto& face        = batchMemory.textures[index];
            createInfo.faces[index] = TextureMemoryView{
                .width    = face.width,
                .height   = face.height,
                .channels = face.channels,
                .format   = face.format,
                .data     = face.bytes.data(),
                .dataSize = face.bytes.size(),
            };
        }

        auto* render = system.getRender();
        auto cubemap = render ? Texture::createCubeMapFromMemory(*render, createInfo) : nullptr;
        if (!cubemap || !cubemap->isValid()) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap creation failed");
            failEnvironmentDerivedBranches(component, state, "cubemap creation failed");
            return;
        }

        state.cubemapTexture = std::move(cubemap);
        state.cubemapRenderImage.reset();
        completeEnvironmentSource(system.getRender(), component, state, "cubemap source resolved");
        return;
    }

    if (component.hasCylindricalSource()) {
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            state.pendingCylindricalFuture = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                .filepath        = component.cylindricalSource.filepath,
                .name            = "EnvironmentLightingCylindricalSource",
                .onReady         = {},
                .colorSpace      = AssetManager::ETextureColorSpace::Linear,
                .textureSemantic = std::nullopt,
            });
        }
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            return;
        }

        const auto sourceTexture = state.pendingCylindricalFuture->getShared();
        state.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            detail::retireEnvTextures(state);
            transition.fail("cylindrical source invalid");
            failEnvironmentDerivedBranches(component, state, "cylindrical source invalid");
            return;
        }

        auto job = createEnvironmentCubemapJob(system, entity, component, sourceTexture);
        if (!job) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap job not wired");
            failEnvironmentDerivedBranches(component, state, "cubemap job not wired");
            return;
        }

        state.pendingEnvironmentOffscreen = std::move(job);
        transition.to(EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap, "queue environment preprocess");
        return;
    }

    detail::resetEnvPending(state);
    transition.to(EEnvironmentLightingSourceResolveState::Empty, "active source changed while resolving");
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                        : EEnvironmentLightingIrradianceResolveState::Disabled,
            "active source changed while resolving");
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                       : EEnvironmentLightingPrefilterResolveState::Disabled,
            "active source changed while resolving");
}

void handleEnvironmentSourceBuildingCubemap(const OffscreenJobQueueService&  queueService,
                                            IRender*                         render,
                                            EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (!state.pendingEnvironmentOffscreen) {
        transition.fail("preprocess job missing");
        failEnvironmentDerivedBranches(component, state, "preprocess job missing");
        return;
    }

    if (state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingEnvironmentOffscreen);
        return;
    }

    if (state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingEnvironmentOffscreen->hasFailed() || !state.pendingEnvironmentOffscreen->result ||
        !state.pendingEnvironmentOffscreen->result->outputImage) {
        state.pendingEnvironmentOffscreen.reset();
        detail::retireEnvTextures(state);
        transition.fail("preprocess failed");
        failEnvironmentDerivedBranches(component, state, "preprocess failed");
        return;
    }

    if (!state.pendingEnvironmentOffscreen->isGpuCompleted()) {
        return;
    }

    state.cubemapRenderImage = detail::adoptRenderTexture(state.pendingEnvironmentOffscreen->result->outputImage);
    detail::retireTextureNow(state.cubemapTexture);
    state.pendingEnvironmentOffscreen.reset();
    completeEnvironmentSource(render, component, state, "environment cubemap preprocess completed");
}

void handleEnvironmentIrradianceDirty(EnvironmentLightingProcessor&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state,
                                      const SkyboxRuntimeState*        sceneSkyboxState)
{
    const auto sourceCubemap = resolveEnvironmentSourceCubemap(component, state, sceneSkyboxState);
    if (!component.bEnableIrradiance || state.sourceState != EEnvironmentLightingSourceResolveState::Ready || !sourceCubemap || !sourceCubemap->isValid()) {
        return;
    }

    tryBeginEnvIrradianceJob(system,
                             entity,
                             component,
                             state,
                             sourceCubemap);
}

void handleEnvironmentIrradianceBuilding(const OffscreenJobQueueService&  queueService,
                                         IRender*                         render,
                                         EnvironmentLightingComponent&    component,
                                         EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingIrradianceOffscreen) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess job missing");
        return;
    }

    if (state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingIrradianceOffscreen);
        return;
    }

    if (state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingIrradianceOffscreen->hasFailed() ||
        !state.pendingIrradianceOffscreen->result ||
        !state.pendingIrradianceOffscreen->result->outputImage) {
        state.pendingIrradianceOffscreen.reset();
        detail::retireRenderTextureNow(state.irradianceRenderImage);
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess failed");
        return;
    }

    if (!state.pendingIrradianceOffscreen->isGpuCompleted()) {
        return;
    }

    // ok
    state.irradianceRenderImage = detail::adoptRenderTexture(state.pendingIrradianceOffscreen->result->outputImage);
    detail::rebuildEnvironmentIrradianceViews(render, state);
    state.pendingIrradianceOffscreen.reset();
    ++state.resultVersion;
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Ready, "irradiance preprocess completed");
}

void handleEnvironmentPrefilterDirty(EnvironmentLightingProcessor&           system,
                                     entt::entity                     entity,
                                     EnvironmentLightingComponent&    component,
                                     EnvironmentLightingRuntimeState& state,
                                     const SkyboxRuntimeState*        sceneSkyboxState)
{
    const auto sourceCubemap = resolveEnvironmentSourceCubemap(component, state, sceneSkyboxState);
    if (!component.bEnablePrefilter || state.sourceState != EEnvironmentLightingSourceResolveState::Ready || !sourceCubemap || !sourceCubemap->isValid()) {
        return;
    }

    tryBeginEnvPrefilterJob(system,
                            entity,
                            component,
                            state,
                            sourceCubemap);
}

void handleEnvironmentPrefilterBuilding(const OffscreenJobQueueService&  queueService,
                                        IRender*                         render,
                                        EnvironmentLightingComponent&    component,
                                        EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingPrefilterOffscreen) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess job missing");
        return;
    }

    if (state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingPrefilterOffscreen);
        return;
    }

    if (state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingPrefilterOffscreen->hasFailed() || !state.pendingPrefilterOffscreen->result ||
        !state.pendingPrefilterOffscreen->result->outputImage) {
        state.pendingPrefilterOffscreen.reset();
        detail::retireRenderTextureNow(state.prefilterRenderImage);
        detail::rebuildPrefilterViews(render, state);
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess failed");
        return;
    }

    if (!state.pendingPrefilterOffscreen->isGpuCompleted()) {
        return;
    }

    state.prefilterRenderImage = detail::adoptRenderTexture(state.pendingPrefilterOffscreen->result->outputImage);
    detail::rebuildPrefilterViews(render, state);
    state.pendingPrefilterOffscreen.reset();
    ++state.resultVersion;
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Ready, "prefilter preprocess completed");
}

void resolveEnvironmentSourceState(EnvironmentLightingProcessor&           system,
                                   entt::entity                     entity,
                                   EnvironmentLightingComponent&    component,
                                   EnvironmentLightingRuntimeState& state)
{
    switch (state.sourceState) {
    case EEnvironmentLightingSourceResolveState::Dirty:
    {
        handleEnvironmentSourceDirty(component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::ResolvingSource:
    {
        handleEnvironmentSourceResolving(system, entity, component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap:
    {
        handleEnvironmentSourceBuildingCubemap(system.getOffscreenJobQueueService(), system.getRender(), component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::Empty:
    case EEnvironmentLightingSourceResolveState::Ready:
    case EEnvironmentLightingSourceResolveState::Failed:
    default:
    {
    } break;
    }
}

void resolveEnvironmentIrradianceState(EnvironmentLightingProcessor&           system,
                                       entt::entity                     entity,
                                       EnvironmentLightingComponent&    component,
                                       EnvironmentLightingRuntimeState& state,
                                       const SkyboxRuntimeState*        sceneSkyboxState)
{
    switch (state.irradianceState) {
    case EEnvironmentLightingIrradianceResolveState::Dirty:
    {
        handleEnvironmentIrradianceDirty(system, entity, component, state, sceneSkyboxState);
    } break;
    case EEnvironmentLightingIrradianceResolveState::Building:
    {
        handleEnvironmentIrradianceBuilding(system.getOffscreenJobQueueService(), system.getRender(), component, state);
    } break;
    case EEnvironmentLightingIrradianceResolveState::Empty:
    case EEnvironmentLightingIrradianceResolveState::Disabled:
    case EEnvironmentLightingIrradianceResolveState::Ready:
    case EEnvironmentLightingIrradianceResolveState::Failed:
    default:
    {
    } break;
    }
}

void resolveEnvironmentPrefilterState(EnvironmentLightingProcessor&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state,
                                      const SkyboxRuntimeState*        sceneSkyboxState)
{
    switch (state.prefilterState) {
    case EEnvironmentLightingPrefilterResolveState::Dirty:
    {
        handleEnvironmentPrefilterDirty(system, entity, component, state, sceneSkyboxState);
    } break;
    case EEnvironmentLightingPrefilterResolveState::Building:
    {
        handleEnvironmentPrefilterBuilding(system.getOffscreenJobQueueService(), system.getRender(), component, state);
    } break;
    case EEnvironmentLightingPrefilterResolveState::Empty:
    case EEnvironmentLightingPrefilterResolveState::Disabled:
    case EEnvironmentLightingPrefilterResolveState::Ready:
    case EEnvironmentLightingPrefilterResolveState::Failed:
    default:
    {
    } break;
    }
}

} // namespace

void EnvironmentLightingProcessor::resolvePendingEnvironmentLighting(Scene* scene)
{
    YA_PROFILE_FUNCTION();
    auto&       registry         = scene->getRegistry();
    auto*       assets           = AssetManager::get();
    const auto* sceneSkyboxState = findFirstSceneSkyboxState(scene);

    auto pumpOne = [&](entt::entity entity) {
        YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Entity");
        if (!registry.valid(entity) || !registry.all_of<EnvironmentLightingComponent>(entity)) {
            cleanupEnvironmentLightingState(entity);
            return;
        }

        auto& elc          = registry.get<EnvironmentLightingComponent>(entity);
        auto& pendingState = _environmentStates[entity];
        const auto previousResultVersion = pendingState.resultVersion;
        const std::string derivedKey = buildEnvironmentDerivedKey(elc, assets, sceneSkyboxState);

        if (pendingState.authoringVersion != elc.authoringVersion) {
            detail::resetEnvState(pendingState);
            pendingState.authoringVersion = elc.authoringVersion;
            pendingState.lastStartedAuthoringVersion = elc.authoringVersion;
        }

        if (elc.usesSceneSkybox()) {
            _sceneSkyboxEnvironmentDependents.insert(entity);
        }
        else {
            _sceneSkyboxEnvironmentDependents.erase(entity);
        }

        if (!elc.hasSource()) {
            handleEnvironmentNoSource(elc, pendingState);
            pendingState.derivedKey.clear();
            pendingState.boundResource.reset();
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
            _activeEnvironment.erase(entity);
            return;
        }

        if (!derivedKey.empty() && !pendingState.derivedKey.empty() && pendingState.derivedKey != derivedKey) {
            detail::resetEnvState(pendingState);
            pendingState.resultVersion = 0;
            makeTransition(pendingState.sourceState, "EnvironmentLighting.Source")
                .to(EEnvironmentLightingSourceResolveState::Dirty, "derived key changed");
            makeTransition(pendingState.irradianceState, "EnvironmentLighting.Irradiance")
                .to(elc.bEnableIrradiance
                        ? EEnvironmentLightingIrradianceResolveState::Dirty
                        : EEnvironmentLightingIrradianceResolveState::Disabled,
                    "derived key changed");
            makeTransition(pendingState.prefilterState, "EnvironmentLighting.Prefilter")
                .to(elc.bEnablePrefilter
                        ? EEnvironmentLightingPrefilterResolveState::Dirty
                        : EEnvironmentLightingPrefilterResolveState::Disabled,
                    "derived key changed");
        }

        if (!derivedKey.empty()) {
            if (auto it = _environmentDerivedResources.find(derivedKey); it != _environmentDerivedResources.end() &&
                it->second && canUseEnvironmentDerivedResource(elc, *it->second)) {
                const bool bCacheRebound = pendingState.resultVersion == 0 ||
                                           pendingState.derivedKey != derivedKey ||
                                           pendingState.boundResource.get() != it->second.get() ||
                                           pendingState.sourceState != EEnvironmentLightingSourceResolveState::Ready ||
                                           (elc.bEnableIrradiance && pendingState.irradianceState != EEnvironmentLightingIrradianceResolveState::Ready) ||
                                           (elc.bEnablePrefilter && pendingState.prefilterState != EEnvironmentLightingPrefilterResolveState::Ready);
                it->second->lastUsedFrame = _getFrameIndex ? _getFrameIndex() : 0;
                applyEnvironmentResource(it->second, pendingState);
                pendingState.derivedKey = derivedKey;
                if (bCacheRebound) {
                    makeTransition(pendingState.sourceState, "EnvironmentLighting.Source")
                        .to(EEnvironmentLightingSourceResolveState::Ready, "derived cache hit");
                    makeTransition(pendingState.irradianceState, "EnvironmentLighting.Irradiance")
                        .to(elc.bEnableIrradiance
                                ? EEnvironmentLightingIrradianceResolveState::Ready
                                : EEnvironmentLightingIrradianceResolveState::Disabled,
                            "derived cache hit");
                    makeTransition(pendingState.prefilterState, "EnvironmentLighting.Prefilter")
                        .to(elc.bEnablePrefilter
                                ? EEnvironmentLightingPrefilterResolveState::Ready
                                : EEnvironmentLightingPrefilterResolveState::Disabled,
                            "derived cache hit");
                    ++pendingState.resultVersion;
                }
                pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
                _activeEnvironment.erase(entity);
                return;
            }
        }

        if (pendingState.sourceState == EEnvironmentLightingSourceResolveState::Dirty ||
            pendingState.sourceState == EEnvironmentLightingSourceResolveState::Empty) {
            detail::resetEnvPending(pendingState);
            pendingState.lastStartedAuthoringVersion = elc.authoringVersion;
        }

        syncEnvironmentDerivedBranchEnablement(getRender(), elc, pendingState);

        if (const auto* sourceSkyboxState = syncEnvSkybox(elc, pendingState, sceneSkyboxState)) {
            completeEnvironmentSourceFromDependency(elc, pendingState, sourceSkyboxState, "scene skybox source resolved");
        }

        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Source");
            resolveEnvironmentSourceState(*this, entity, elc, pendingState);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Irradiance");
            resolveEnvironmentIrradianceState(*this, entity, elc, pendingState, sceneSkyboxState);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Prefilter");
            resolveEnvironmentPrefilterState(*this, entity, elc, pendingState, sceneSkyboxState);
        }

        if (!derivedKey.empty() && isEnvironmentResolveComplete(elc, pendingState)) {
            pendingState.derivedKey = derivedKey;
            auto resource = snapshotEnvironmentResource(pendingState, _getFrameIndex ? _getFrameIndex() : 0);
            _environmentDerivedResources[derivedKey] = resource;
            pendingState.boundResource = resource;
        }

        const bool bActive = pendingState.sourceState == EEnvironmentLightingSourceResolveState::ResolvingSource ||
                             pendingState.sourceState == EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap ||
                             pendingState.irradianceState == EEnvironmentLightingIrradianceResolveState::Building ||
                             pendingState.prefilterState == EEnvironmentLightingPrefilterResolveState::Building;
        if (bActive) {
            _activeEnvironment.insert(entity);
        }
        else {
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
            _activeEnvironment.erase(entity);
        }

        if (pendingState.resultVersion != previousResultVersion) {
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
        }
    };

    while (!_dirtyEnvironmentQueue.empty()) {
        const auto entity = _dirtyEnvironmentQueue.front();
        _dirtyEnvironmentQueue.pop_front();
        _dirtyEnvironmentSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeEnvironment.begin(), _activeEnvironment.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }

    std::vector<entt::entity> staleEntities;
    staleEntities.reserve(_environmentStates.size());
    for (const auto& [entity, state] : _environmentStates) {
        (void)state;
        if (!registry.valid(entity) || !registry.all_of<EnvironmentLightingComponent>(entity)) {
            staleEntities.push_back(entity);
        }
    }
    for (const auto entity : staleEntities) {
        cleanupEnvironmentLightingState(entity);
    }
}

} // namespace ya
