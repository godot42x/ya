#include "Render3D/Services/GameplayResourceBinding.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/SceneBus.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <type_traits>
#include <vector>

namespace ya
{

namespace
{

#ifdef BUILD_DEBUG
constexpr uint64_t CONSISTENCY_AUDIT_INTERVAL_TICKS = 120;
#endif

bool isMaterialType(ya::type_index_t typeIndex)
{
    return typeIndex == type_index_v<PhongMaterialComponent> ||
           typeIndex == type_index_v<PBRMaterialComponent> ||
           typeIndex == type_index_v<UnlitMaterialComponent>;
}

bool isWatchedType(ya::type_index_t typeIndex)
{
    return isMaterialType(typeIndex) || typeIndex == type_index_v<BillboardComponent>;
}

template <typename Component, typename Fn>
void forEachTextureSlot(Component& component, Fn&& fn)
{
    if constexpr (std::is_same_v<Component, BillboardComponent>) {
        fn(component.image);
    }
    else {
        for (size_t index = 0; index < static_cast<size_t>(Component::slot_enum_t::Count); ++index) {
            if (TextureSlot* slot =
                    component.getTextureSlot(static_cast<typename Component::slot_enum_t>(index))) {
                fn(*slot);
            }
        }
    }
}

} // namespace

void GameplayResourceBinding::init()
{
    if (bBusSubscribed) {
        return;
    }
    SceneBus& bus = SceneBus::get();
    _componentAddedHandle =
        bus.onComponentAdded.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentAdded(reg, entity, type);
        });
    _componentEditedHandle =
        bus.onComponentEdited.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentEdited(reg, entity, type);
        });
    _componentRemovedHandle =
        bus.onComponentRemoved.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentRemoved(reg, entity, type);
        });
    bBusSubscribed = true;
}

void GameplayResourceBinding::shutdown()
{
    dropAllWork();
    if (bBusSubscribed) {
        SceneBus& bus = SceneBus::get();
        bus.onComponentAdded.remove(_componentAddedHandle);
        bus.onComponentEdited.remove(_componentEditedHandle);
        bus.onComponentRemoved.remove(_componentRemovedHandle);
        _componentAddedHandle = _componentEditedHandle = _componentRemovedHandle = INVALID_HANDLE;
        bBusSubscribed = false;
    }
}

void GameplayResourceBinding::onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    SceneWork* work = findWork(&registry);
    if (!work) {
        return;
    }
    if (isMaterialType(typeIndex)) {
        enqueueMaterial(*work, entity);
    }
    else if (typeIndex == type_index_v<BillboardComponent>) {
        enqueueBillboard(*work, entity);
    }
}

void GameplayResourceBinding::onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    onComponentAdded(registry, entity, typeIndex);
}

void GameplayResourceBinding::onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (!isWatchedType(typeIndex)) {
        return;
    }
    if (SceneWork* work = findWork(&registry)) {
        dropEntityWork(*work, entity);
    }
}

GameplayResourceBinding::SceneWork* GameplayResourceBinding::findWork(const entt::registry* registry)
{
    auto it = _sceneWork.find(registry);
    return it != _sceneWork.end() ? &it->second : nullptr;
}

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
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/Billboards");
            resolvePendingBillboards(work);
        }
#ifdef BUILD_DEBUG
        auditSlotSubscriptions(work);
#endif
    }
}

GameplayResourceBinding::SceneWork& GameplayResourceBinding::ensureWork(Scene& scene)
{
    SceneWork& work = _sceneWork[&scene.getRegistry()];
    work.registry   = &scene.getRegistry();
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
    work.dirtyBillboardQueue.clear();
    work.dirtyBillboardSet.clear();
    work.entityWork.clear();
}

void GameplayResourceBinding::dropEntityWork(SceneWork& work, entt::entity entity)
{
    work.dirtyMaterialSet.erase(entity);
    work.dirtyBillboardSet.erase(entity);
    std::erase(work.dirtyMaterialQueue, entity);
    std::erase(work.dirtyBillboardQueue, entity);
    work.entityWork.erase(entity);
}

void GameplayResourceBinding::dropAllWork()
{
    for (auto& [registry, work] : _sceneWork) {
        (void)registry;
        dropWork(work);
    }
    _sceneWork.clear();
}

void GameplayResourceBinding::seedSceneResolveWork(SceneWork& work)
{
    auto& registry = *work.registry;
    for (auto&& [entity, unused] : registry.view<PhongMaterialComponent>().each()) {
        (void)unused;
        enqueueMaterial(work, entity);
    }
    for (auto&& [entity, unused] : registry.view<PBRMaterialComponent>().each()) {
        (void)unused;
        enqueueMaterial(work, entity);
    }
    for (auto&& [entity, unused] : registry.view<UnlitMaterialComponent>().each()) {
        (void)unused;
        enqueueMaterial(work, entity);
    }
    for (auto&& [entity, unused] : registry.view<BillboardComponent>().each()) {
        (void)unused;
        enqueueBillboard(work, entity);
    }
}

void GameplayResourceBinding::enqueueMaterial(SceneWork& work, entt::entity entity)
{
    auto& registry = *work.registry;
    if (!registry.valid(entity) ||
        (!registry.all_of<PhongMaterialComponent>(entity) &&
         !registry.all_of<PBRMaterialComponent>(entity) &&
         !registry.all_of<UnlitMaterialComponent>(entity))) {
        return;
    }
    if (work.dirtyMaterialSet.insert(entity).second) {
        work.dirtyMaterialQueue.push_back(entity);
    }
}

void GameplayResourceBinding::enqueueBillboard(SceneWork& work, entt::entity entity)
{
    auto& registry = *work.registry;
    if (!registry.valid(entity) || !registry.all_of<BillboardComponent>(entity)) {
        return;
    }
    if (work.dirtyBillboardSet.insert(entity).second) {
        work.dirtyBillboardQueue.push_back(entity);
    }
}

void GameplayResourceBinding::enqueueFromSlotUpdate(const entt::registry* registry, entt::entity entity, bool bBillboard)
{
    // The scene may already be destroyed: look the work up by pointer value
    // and touch nothing else when it is gone. The caller's next prepare drops
    // absent scenes before it pumps, so a surviving entry is a live registry.
    SceneWork* work = findWork(registry);
    if (!work) {
        return;
    }
    if (bBillboard) {
        if (work->dirtyBillboardSet.insert(entity).second) {
            work->dirtyBillboardQueue.push_back(entity);
        }
    }
    else if (work->dirtyMaterialSet.insert(entity).second) {
        work->dirtyMaterialQueue.push_back(entity);
    }
}

template <typename Component>
void GameplayResourceBinding::subscribeSlotObservers(SceneWork& work, entt::entity entity, Component& component)
{
    auto& entityWork = work.entityWork[entity];
    forEachTextureSlot(component, [&](TextureSlot& slot) {
        if (!slot.hasPath()) {
            return;
        }
        const AssetHandle<Texture>& handle = slot.textureRef._handle;
        if (!handle) {
            return;
        }
        auto callback = [this, registry = work.registry, entity,
                         bBillboard = std::is_same_v<Component, BillboardComponent>]()
        {
            // Slot updates land on the game thread; the callback only enqueues.
            enqueueFromSlotUpdate(registry, entity, bBillboard);
        };
        entityWork.slotTokens.push_back(SlotSubscription{handle, handle->observers.subscribe(std::move(callback))});
    });
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
    auto& registry = *work.registry;

    while (!work.dirtyMaterialQueue.empty()) {
        const auto entity = work.dirtyMaterialQueue.front();
        work.dirtyMaterialQueue.pop_front();
        work.dirtyMaterialSet.erase(entity);
        if (!registry.valid(entity)) {
            work.entityWork.erase(entity);
            continue;
        }

        // Drop the previous fill subscriptions first: resolve may have rebound
        // paths to different slots, and tokens must not accumulate.
        work.entityWork[entity].slotTokens.clear();
        auto pump = [&](auto* component) {
            if (!component) {
                return;
            }
            component->resolve();
            subscribeSlotObservers(work, entity, *component);
        };
        pump(registry.try_get<PhongMaterialComponent>(entity));
        pump(registry.try_get<PBRMaterialComponent>(entity));
        pump(registry.try_get<UnlitMaterialComponent>(entity));
    }
}

void GameplayResourceBinding::resolvePendingBillboards(SceneWork& work)
{
    auto& registry = *work.registry;

    while (!work.dirtyBillboardQueue.empty()) {
        const auto entity = work.dirtyBillboardQueue.front();
        work.dirtyBillboardQueue.pop_front();
        work.dirtyBillboardSet.erase(entity);
        if (!registry.valid(entity)) {
            work.entityWork.erase(entity);
            continue;
        }

        // Drop the previous fill subscriptions first: resolve may have rebound
        // the image to a different slot.
        work.entityWork[entity].slotTokens.clear();
        if (auto* billboard = registry.try_get<BillboardComponent>(entity)) {
            billboard->resolve();
            subscribeSlotObservers(work, entity, *billboard);
        }
        else {
            work.entityWork.erase(entity);
        }
    }
}

#ifdef BUILD_DEBUG
void GameplayResourceBinding::auditSlotSubscriptions(SceneWork& work)
{
    const uint64_t currentTick = _getHostTick ? _getHostTick() : 0;
    if (work.nextConsistencyAuditTick != 0 && currentTick < work.nextConsistencyAuditTick) {
        return;
    }
    work.nextConsistencyAuditTick = currentTick + CONSISTENCY_AUDIT_INTERVAL_TICKS;

    auto&  registry       = *work.registry;
    auto   enqueueMaterialFn = [this](SceneWork& w, entt::entity e) { enqueueMaterial(w, e); };
    auto   enqueueBillboardFn = [this](SceneWork& w, entt::entity e) { enqueueBillboard(w, e); };
    const auto auditComponent = [&](auto&& view, auto&& enqueue) {
        for (auto&& [entity, component] : view.each()) {
            bool bAnyLoading = false;
            forEachTextureSlot(component, [&](const TextureSlot& slot) {
                if (slot.textureRef.isLoading()) {
                    bAnyLoading = true;
                }
            });
            if (!bAnyLoading) {
                continue;
            }
            // Every component with a loading slot must be queued for a
            // resolve or covered by a held slot subscription. Neither is true
            // means a write path or a fill escaped the funnel.
            if (!work.entityWork.contains(entity) &&
                !work.dirtyMaterialSet.contains(entity) &&
                !work.dirtyBillboardSet.contains(entity)) {
                YA_CORE_ASSERT(false, "ResourceResolve audit: loading texture slot is neither queued nor subscribed");
                YA_CORE_WARN("ResourceResolve audit: re-armed entity {} (loading slot without subscription)",
                             static_cast<uint32_t>(entity));
                enqueue(work, entity);
            }
        }
    };
    auditComponent(registry.view<PhongMaterialComponent>(), enqueueMaterialFn);
    auditComponent(registry.view<PBRMaterialComponent>(), enqueueMaterialFn);
    auditComponent(registry.view<UnlitMaterialComponent>(), enqueueMaterialFn);
    auditComponent(registry.view<BillboardComponent>(), enqueueBillboardFn);
}
#endif

} // namespace ya
