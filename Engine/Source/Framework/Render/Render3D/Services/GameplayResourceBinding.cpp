#include "GameplayResourceBinding.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "Gameplay/Systems/Components/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene/Core/Scene.h"

#include <vector>

namespace ya
{

void GameplayResourceBinding::init()
{
}

void GameplayResourceBinding::clearSceneResolveWork()
{
    _dirtyMaterialQueue.clear();
    _dirtyMaterialSet.clear();
    _activeMaterial.clear();
    _nextMaterialAuditFrame = 0;
    _pendingStateScene = nullptr;
}

bool GameplayResourceBinding::isMaterialQueuedOrActive(entt::entity entity) const
{
    return _dirtyMaterialSet.contains(entity) || _activeMaterial.contains(entity);
}

void GameplayResourceBinding::auditMaterialWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    const uint64_t currentFrame = _getFrameIndex ? _getFrameIndex() : 0;
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

void GameplayResourceBinding::cleanupMaterialState(entt::entity entity)
{
    _dirtyMaterialSet.erase(entity);
    _activeMaterial.erase(entity);
    std::erase(_dirtyMaterialQueue, entity);
}

void GameplayResourceBinding::markMaterialDirty(entt::entity entity, const char* reason)
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

void GameplayResourceBinding::resolvePendingMeshes(Scene* scene)
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

void GameplayResourceBinding::resolvePendingMaterials(Scene* scene)
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

void GameplayResourceBinding::resolvePendingUI(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<UIComponent>().each([&](auto entity, UIComponent& uiComponent) {
        (void)entity;
        if (!uiComponent.view.textureRef.isLoaded() && uiComponent.view.textureRef.hasPath()) {
            uiComponent.view.textureRef.resolve();
        }
    });
}

void GameplayResourceBinding::resolvePendingBillboards(Scene* scene)
{
    auto& registry = scene->getRegistry();

    for (const auto& [entity, comp] : registry.view<BillboardComponent>().each()) {
        (void)entity;
        if (comp.bDirty) {
            comp.resolve();
        }
    }
}



void GameplayResourceBinding::shutdown()
{
    clearSceneResolveWork();
    _getActiveScene = {};
}

void GameplayResourceBinding::onUpdate(float dt)
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
        for (auto&& [entity, unused] : scene->getRegistry().view<PhongMaterialComponent>().each()) {
            (void)unused;
            markMaterialDirty(entity, "scene seed");
        }
        for (auto&& [entity, unused] : scene->getRegistry().view<PBRMaterialComponent>().each()) {
            (void)unused;
            markMaterialDirty(entity, "scene seed");
        }
        for (auto&& [entity, unused] : scene->getRegistry().view<UnlitMaterialComponent>().each()) {
            (void)unused;
            markMaterialDirty(entity, "scene seed");
        }
    }

    {
        YA_PROFILE_SCOPE("ResourceResolve/Meshes");
        resolvePendingMeshes(scene);
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
}

} // namespace ya
