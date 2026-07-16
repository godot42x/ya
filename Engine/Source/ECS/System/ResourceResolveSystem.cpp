#include "ResourceResolveSystem.Detail.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Runtime/App/App.h"
#include "Scene/SceneManager.h"

namespace ya
{

namespace
{

template <typename TMaterialComponent>
void resolvePendingMaterialComponents(entt::registry& registry)
{
    registry.view<TMaterialComponent>().each([](auto entity, TMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
        else if (materialComponent.isResolved()) {
            materialComponent.checkTexturesStaleness();
        }
    });
}

} // namespace

void ResourceResolveSystem::init()
{
    _equidistantCylindrical2CubeMap.init(App::get()->getRender());
    _cubeMap2IrradianceMap.init(App::get()->getRender());
    _cubeMap2PrefilterPipeline.init(App::get()->getRender());
}

void ResourceResolveSystem::clearPendingResolveStates()
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
    _pendingStateScene = nullptr;
}

void ResourceResolveSystem::shutdown()
{
    clearPendingResolveStates();
    _cubeMap2PrefilterPipeline.shutdown();
    _cubeMap2IrradianceMap.shutdown();
    _equidistantCylindrical2CubeMap.shutdown();
}

void ResourceResolveSystem::onUpdate(float dt)
{
    (void)dt;

    auto* const sceneManager = App::get()->getSceneManager();
    auto* const scene        = sceneManager->getActiveScene();
    if (!scene) {
        clearPendingResolveStates();
        return;
    }

    if (_pendingStateScene != scene) {
        clearPendingResolveStates();
        _pendingStateScene = scene;
    }

    resolvePendingMeshes(scene);
    resolvePendingMaterials(scene);
    resolvePendingUI(scene);
    resolvePendingBillboards(scene);
    resolvePendingSkybox(scene);
    resolvePendingEnvironmentLighting(scene);
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

void ResourceResolveSystem::resolvePendingMaterials(Scene* scene)
{
    auto& registry = scene->getRegistry();

    resolvePendingMaterialComponents<PhongMaterialComponent>(registry);
    resolvePendingMaterialComponents<PBRMaterialComponent>(registry);
    resolvePendingMaterialComponents<UnlitMaterialComponent>(registry);
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

const SkyboxRuntimeState* ResourceResolveSystem::findSkyboxState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? nullptr : &it->second;
}

const SkyboxRuntimeState* ResourceResolveSystem::findFirstSceneSkyboxState(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
        const auto* state = findSkyboxState(entity);
        if (sc.resolveState == ESkyboxResolveState::Ready && state && state->hasRenderableCubemap()) {
            return state;
        }
    }

    return nullptr;
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
        if (elc.hasReadyIrradiance() && state && state->hasIrradianceMap()) {
            return state;
        }
    }

    return nullptr;
}

stdptr<Texture> ResourceResolveSystem::findSceneSkyboxTextureShared(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapTexture : nullptr;
}

std::shared_ptr<RenderImage> ResourceResolveSystem::findSceneSkyboxRenderImageShared(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapRenderImage : nullptr;
}

EnvironmentLightingSceneResources ResourceResolveSystem::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    EnvironmentLightingSceneResources resources{};
    if (!scene) {
        return resources;
    }

    const auto* skyboxState = findFirstSceneSkyboxState(scene);
    if (skyboxState) {
        resources.cubemapRenderImage = skyboxState->cubemapRenderImage;
        resources.cubemapTexture     = skyboxState->cubemapTexture;
    }

    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (!state) {
            continue;
        }

        if (elc.hasReadySource()) {
            if (elc.usesSceneSkybox()) {
                resources.cubemapRenderImage = skyboxState ? skyboxState->cubemapRenderImage : nullptr;
                resources.cubemapTexture     = skyboxState ? skyboxState->cubemapTexture : nullptr;
            }
            else if (state->hasRenderableCubemap()) {
                resources.cubemapRenderImage = state->cubemapRenderImage;
                resources.cubemapTexture     = state->cubemapTexture;
            }
        }

        if (!resources.irradianceTexture && elc.hasReadyIrradiance() && state->hasIrradianceMap()) {
            resources.irradianceRenderImage = state->irradianceRenderImage;
            resources.irradianceTexture     = state->irradianceTexture;
        }

        if (!resources.prefilterTexture && elc.hasReadyPrefilter() && state->hasPrefilterMap()) {
            resources.prefilterRenderImage = state->prefilterRenderImage;
            resources.prefilterTexture     = state->prefilterTexture;
        }

        if (resources.cubemapTexture && resources.irradianceTexture && resources.prefilterTexture) {
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
    info.cubemapTexture        = state->cubemapTexture.get();
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

    info.cubemapRenderImage    = state->cubemapRenderImage;
    info.cubemapTexture        = state->cubemapTexture.get();
    info.irradianceRenderImage = state->irradianceRenderImage;
    info.irradianceTexture     = state->irradianceTexture.get();
    info.prefilterRenderImage  = state->prefilterRenderImage;
    info.prefilterTexture      = state->prefilterTexture.get();
    info.prefilterMipCount     = state->prefilterPreviewMipCount;
    info.bHasRenderableCubemap = state->hasRenderableCubemap();
    info.bHasIrradianceMap     = state->hasIrradianceMap();
    info.bHasPrefilterMap      = state->hasPrefilterMap();

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        info.cubemapFaceViews[faceIndex]    = state->cubemapFacePreviewViews[faceIndex].get();
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
