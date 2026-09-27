#include "Render3D/Services/GameplayResourceBinding.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/Sprite2DComponent.h"
#include "ECS/Systems/Components/UIComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <vector>

namespace ya
{

void GameplayResourceBinding::prepareScenes(std::span<Scene* const> scenes, float dt)
{
    YA_PROFILE_FUNCTION();

    (void)dt;

    // A Scene this tick does not prepare is not one of the Scenes the last
    // tick left behind, so its work goes here. Preparing one Scene can
    // therefore never take another Scene's work with it.
    dropScenesAbsentFrom(scenes);

    for (Scene* scene : scenes) {
        if (!scene) {
            continue;
        }

        SceneWork& work = ensureWork(*scene);
        if (!work.bSeeded) {
            work.bSeeded = true;
            seedSceneResolveWork(work);
        }

        {
            YA_PROFILE_SCOPE("ResourceResolve/Meshes");
            resolvePendingMeshes(*scene);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/Materials");
            resolvePendingMaterials(work);
            // Periodic staleness / missed-enqueue audit runs after the
            // per-frame sweep so freshly modified components are already
            // queued and only true bypasses trip the dev assertion.
            auditMaterialWork(work);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/UI");
            resolvePendingUI(*scene);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/Billboards");
            resolvePendingBillboards(*scene);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/Sprites");
            resolvePendingSprites(*scene);
        }
    }
}

GameplayResourceBinding::SceneWork& GameplayResourceBinding::ensureWork(Scene& scene)
{
    SceneWork& work = _sceneWork[&scene];
    work.scene      = &scene;
    return work;
}

void GameplayResourceBinding::dropScenesAbsentFrom(std::span<Scene* const> scenes)
{
    for (auto it = _sceneWork.begin(); it != _sceneWork.end();) {
        if (std::ranges::find(scenes, it->second.scene) != scenes.end()) {
            ++it;
            continue;
        }
        dropWork(it->second);
        it = _sceneWork.erase(it);
    }
}

void GameplayResourceBinding::dropWork(SceneWork& work)
{
    work.dirtyMaterialQueue.clear();
    work.dirtyMaterialSet.clear();
    work.activeMaterial.clear();
    work.nextMaterialAuditTick = 0;
}

void GameplayResourceBinding::dropAllWork()
{
    for (auto& [scene, work] : _sceneWork) {
        (void)scene;
        dropWork(work);
    }
    _sceneWork.clear();
}

void GameplayResourceBinding::seedSceneResolveWork(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();
    for (auto&& [entity, unused] : registry.view<PhongMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(work, entity, "scene seed");
    }
    for (auto&& [entity, unused] : registry.view<PBRMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(work, entity, "scene seed");
    }
    for (auto&& [entity, unused] : registry.view<UnlitMaterialComponent>().each()) {
        (void)unused;
        markMaterialDirty(work, entity, "scene seed");
    }
}

bool GameplayResourceBinding::isMaterialQueuedOrActive(const SceneWork& work, entt::entity entity) const
{
    return work.dirtyMaterialSet.contains(entity) || work.activeMaterial.contains(entity);
}

void GameplayResourceBinding::auditMaterialWork(SceneWork& work)
{
    const uint64_t currentTick = _getHostTick ? _getHostTick() : 0;
    if (work.nextMaterialAuditTick != 0 && currentTick < work.nextMaterialAuditTick) {
        return;
    }
    work.nextMaterialAuditTick = currentTick + MATERIAL_AUDIT_INTERVAL_TICKS;

    auto& registry = work.scene->getRegistry();

    const auto auditMaterial = [&](auto&& view) {
        for (auto&& [entity, material] : view.each()) {
            (void)material;
            // The per-frame needsResolve sweep should have queued every
            // component that needs work. A component still unqueued here
            // means a modification path bypassed the dirty queue — surface
            // it in dev builds, self-heal in release.
            if (material.needsResolve() && !isMaterialQueuedOrActive(work, entity)) {
                YA_CORE_ASSERT(false, "ResourceResolve audit: material needs resolve but was not queued");
                YA_CORE_WARN("ResourceResolve audit re-queued Material entity {}: missed enqueue",
                             static_cast<uint32_t>(entity));
                markMaterialDirty(work, entity, "audit: missed material enqueue");
            }
            // Texture staleness (hot reload) is only detected by the periodic
            // audit; mark dirty so the next pump re-resolves the component.
            if (material.isResolved() && material.checkTexturesStaleness()) {
                markMaterialDirty(work, entity, "audit: texture stale");
            }
        }
    };
    auditMaterial(registry.view<PhongMaterialComponent>());
    auditMaterial(registry.view<PBRMaterialComponent>());
    auditMaterial(registry.view<UnlitMaterialComponent>());
}

void GameplayResourceBinding::cleanupMaterialState(SceneWork& work, entt::entity entity)
{
    work.dirtyMaterialSet.erase(entity);
    work.activeMaterial.erase(entity);
    std::erase(work.dirtyMaterialQueue, entity);
}

void GameplayResourceBinding::markMaterialDirty(SceneWork& work, entt::entity entity, const char* reason)
{
    auto& registry = work.scene->getRegistry();
    if (!registry.valid(entity) ||
        (!registry.all_of<PhongMaterialComponent>(entity) &&
         !registry.all_of<PBRMaterialComponent>(entity) &&
         !registry.all_of<UnlitMaterialComponent>(entity))) {
        cleanupMaterialState(work, entity);
        return;
    }

    (void)reason;
    if (work.dirtyMaterialSet.insert(entity).second) {
        work.dirtyMaterialQueue.push_back(entity);
    }
}

void GameplayResourceBinding::resolvePendingMeshes(Scene& scene)
{
    auto& registry = scene.getRegistry();

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

void GameplayResourceBinding::resolvePendingMaterials(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();

    // Per-frame O(1) sweep: components that were just created or modified
    // (constructor / invalidate / reflection setter set the Dirty state
    // without notifying the resolver) are enqueued here so they resolve on
    // the next frame. No string normalization or staleness work happens in
    // this sweep — that stays in the periodic audit.
    const auto sweepNeedsResolve = [&](auto&& view) {
        for (auto&& [entity, material] : view.each()) {
            (void)material;
            if (material.needsResolve() && !isMaterialQueuedOrActive(work, entity)) {
                markMaterialDirty(work, entity, "needs-resolve sweep");
            }
        }
    };
    sweepNeedsResolve(registry.view<PhongMaterialComponent>());
    sweepNeedsResolve(registry.view<PBRMaterialComponent>());
    sweepNeedsResolve(registry.view<UnlitMaterialComponent>());

    auto pumpOne = [&](entt::entity entity) {
        if (!registry.valid(entity)) {
            cleanupMaterialState(work, entity);
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
            cleanupMaterialState(work, entity);
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
            work.activeMaterial.insert(entity);
        }
        else {
            work.activeMaterial.erase(entity);
        }
    };

    while (!work.dirtyMaterialQueue.empty()) {
        const auto entity = work.dirtyMaterialQueue.front();
        work.dirtyMaterialQueue.pop_front();
        work.dirtyMaterialSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(work.activeMaterial.begin(), work.activeMaterial.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

void GameplayResourceBinding::resolvePendingUI(Scene& scene)
{
    auto& registry = scene.getRegistry();

    registry.view<UIComponent>().each([&](auto entity, UIComponent& uiComponent) {
        (void)entity;
        if (!uiComponent.view.textureRef.isLoaded() && uiComponent.view.textureRef.hasPath()) {
            uiComponent.view.textureRef.resolve();
        }
    });
}

void GameplayResourceBinding::resolvePendingBillboards(Scene& scene)
{
    auto& registry = scene.getRegistry();

    for (const auto& [entity, comp] : registry.view<BillboardComponent>().each()) {
        (void)entity;
        if (comp.bDirty) {
            comp.resolve();
        }
    }
}

void GameplayResourceBinding::resolvePendingSprites(Scene& scene)
{
    auto& registry = scene.getRegistry();

    for (const auto& [entity, comp] : registry.view<Sprite2DComponent>().each()) {
        (void)entity;
        if (comp.image.needsResolve()) {
            (void)comp.image.resolve();
        }
    }
}



void GameplayResourceBinding::shutdown()
{
    dropAllWork();
}

} // namespace ya
