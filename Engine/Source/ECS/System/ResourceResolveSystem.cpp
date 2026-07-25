#include "ResourceResolveSystem.Detail.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Runtime/Application/App.h"
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
    _equidistantCylindrical2CubeMap.init(App::get()->getRenderServices().getRender());
    _cubeMap2IrradianceMap.init(App::get()->getRenderServices().getRender());
    _cubeMap2PrefilterPipeline.init(App::get()->getRenderServices().getRender());
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

    auto* const sceneManager = App::get()->getSceneServices().getSceneManager();
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

        if (elc.hasReadySource()) {
            if (elc.usesSceneSkybox()) {
                resources.cubemap.renderImage = skyboxState ? skyboxState->cubemapRenderImage : nullptr;
                resources.cubemap.texture     = skyboxState ? skyboxState->cubemapTexture : nullptr;
            }
            else if (state->hasRenderableCubemap()) {
                resources.cubemap.renderImage = state->cubemapRenderImage;
                resources.cubemap.texture     = state->cubemapTexture;
            }
        }

        if (!resources.irradiance.isValid() && elc.hasReadyIrradiance() && state->hasIrradianceMap()) {
            resources.irradiance.renderImage = state->irradianceRenderImage;
        }

        if (!resources.prefilter.isValid() && elc.hasReadyPrefilter() && state->hasPrefilterMap()) {
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
