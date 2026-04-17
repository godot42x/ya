#include "ResourceResolveSystem.Detail.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
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

    registry.view<MeshComponent>().each([&](auto entity, MeshComponent& meshComponent) {
        (void)entity;
        if (!meshComponent.isResolved() && meshComponent.hasMeshSource()) {
            meshComponent.resolve();
        }
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

Texture* ResourceResolveSystem::findSceneSkyboxTexture(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapTexture.get() : nullptr;
}

Texture* ResourceResolveSystem::findSceneEnvironmentCubemapTexture(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    const auto* skyboxState = findFirstSceneSkyboxState(scene);
    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (!state || !elc.hasReadySource()) {
            continue;
        }

        if (elc.usesSceneSkybox()) {
            return skyboxState ? skyboxState->cubemapTexture.get() : nullptr;
        }

        if (state->hasRenderableCubemap()) {
            return state->cubemapTexture.get();
        }
    }

    return skyboxState ? skyboxState->cubemapTexture.get() : nullptr;
}

Texture* ResourceResolveSystem::findSceneEnvironmentIrradianceTexture(Scene* scene) const
{
    const auto* state = findFirstSceneEnvironmentLightingState(scene);
    return state ? state->irradianceTexture.get() : nullptr;
}

Texture* ResourceResolveSystem::findSceneEnvironmentPrefilterTexture(Scene* scene) const
{
    const auto* state = findFirstSceneEnvironmentLightingState(scene);
    return state ? state->prefilterTexture.get() : nullptr;
}

stdptr<Texture> ResourceResolveSystem::findSceneSkyboxTextureShared(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapTexture : nullptr;
}

stdptr<Texture> ResourceResolveSystem::findSceneEnvironmentIrradianceTextureShared(Scene* scene) const
{
    const auto* state = findFirstSceneEnvironmentLightingState(scene);
    return state ? state->irradianceTexture : nullptr;
}

SkyboxPreviewInfo ResourceResolveSystem::getSkyboxPreview(entt::entity entity) const
{
    SkyboxPreviewInfo info{};

    const auto* state = findSkyboxState(entity);
    if (!state) {
        return info;
    }

    info.sourcePreviewTexture  = state->sourcePreviewTexture.get();
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

    info.cubemapTexture        = state->cubemapTexture.get();
    info.irradianceTexture     = state->irradianceTexture.get();
    info.prefilterTexture      = state->prefilterTexture.get();
    info.bHasRenderableCubemap = state->hasRenderableCubemap();
    info.bHasIrradianceMap     = state->hasIrradianceMap();
    info.bHasPrefilterMap      = state->hasPrefilterMap();

    return info;
}

} // namespace ya
