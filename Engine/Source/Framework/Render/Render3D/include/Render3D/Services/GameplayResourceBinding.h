
#pragma once

#include "Core/Api.h"
#include "Core/Common/AssetSlot.h"
#include "Core/Common/Types.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"

#include <deque>
#include <functional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;
struct Texture;
struct TextureSlot;
struct Model;

/**
 * @brief Runtime resource resolution for the components a Scene already has.
 *
 * Discovery is fully event-driven: the scene edit funnel (SceneBus
 * onComponentAdded / onComponentEdited, fed by Scene::notifyComponentEdited
 * and the creation funnel) enqueues material and billboard entities, and the
 * processor holds the per-entity texture-slot observer tokens whose fills
 * re-enqueue their entity. Steady state touches no component views.
 *
 * The Scene is an argument everywhere, never a lookup: the tick names the
 * Scenes it renders and each one keeps its own queues and subscriptions, so
 * resolving one Scene can neither drop nor overwrite another's.
 */
struct YA_RENDER_3D_API GameplayResourceBinding : public ISystem
{
  public:
    /// A slot update subscription held for one entity (texture or model
    /// slot). The handle keeps the observed slot alive until after the token
    /// dies, so token teardown can never touch a destroyed observers list
    /// (the component's own ref may already be gone by the time this entry
    /// is dropped).
    struct SlotSubscription
    {
        std::shared_ptr<const void> handle;
        AssetObservers::Token       token;
    };

    /// Per-entity derived state: the slot subscriptions that re-enqueue it.
    struct EntityWork
    {
        std::vector<SlotSubscription> slotTokens;
    };

    /// One Scene's material / billboard / mesh work.
    struct SceneWork
    {
        entt::registry*    registry = nullptr;
        Scene*             scene    = nullptr;
        bool               bSeeded  = false;
        std::deque<entt::entity>         dirtyMaterialQueue;
        std::unordered_set<entt::entity> dirtyMaterialSet;
        std::deque<entt::entity>         dirtyBillboardQueue;
        std::unordered_set<entt::entity> dirtyBillboardSet;
        std::deque<entt::entity>         dirtyMeshQueue;
        std::unordered_set<entt::entity> dirtyMeshSet;
        std::unordered_map<entt::entity, EntityWork> entityWork;

#ifdef BUILD_DEBUG
        uint64_t nextConsistencyAuditTick = 0;
#endif
    };

    /// Which queue a slot update re-enqueues its entity into.
    enum class EDirtyWork : uint8_t
    {
        Material,
        Billboard,
        Mesh,
    };

    /// Frame counter for the dev-only consistency audit; bound by the Host.
    void setHostTickProvider(std::function<uint64_t()> provider) { _getHostTick = std::move(provider); }

    /**
     * @brief Resolves the pending materials, billboards and meshes of
     * exactly these Scenes. Texture refs bind their shared asset slot when
     * their path is set, so sprites, tilemaps and UI images need no work
     * here. Skybox / environment / terrain derived GPU work lives in
     * EnvironmentLightingProcessor and TerrainProcessor. A Scene this call
     * does not name has its work dropped.
     */
    void prepareScenes(std::span<Scene* const> scenes, float dt);

    void init() override;
    void shutdown() override;

  private:
    // SceneBus subscriptions. Handlers may arrive for any registry; work is
    // found by the registry pointer (the key, never dereferenced before the
    // lookup succeeds), so a callback racing a scene teardown is inert.
    void onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);

    SceneWork* findWork(const entt::registry* registry);
    SceneWork& ensureWork(Scene& scene);
    void       dropScenesAbsentFrom(std::span<Scene* const> scenes);
    void       dropWork(SceneWork& work);
    void       dropEntityWork(SceneWork& work, entt::entity entity);
    void       dropAllWork();

    void seedSceneResolveWork(SceneWork& work);

    void enqueueMaterial(SceneWork& work, entt::entity entity);
    void enqueueBillboard(SceneWork& work, entt::entity entity);
    void enqueueMesh(SceneWork& work, entt::entity entity);
    /// Slot-update callbacks may run while their scene is already destroyed;
    /// this only touches the work map key, so it stays inert.
    void enqueueFromSlotUpdate(const entt::registry* registry, entt::entity entity, EDirtyWork kind);

    /// Subscribe a fill observer on every path-bearing texture slot of the
    /// component (materials via their slot enums, the billboard via `image`).
    template <typename Component>
    void subscribeSlotObservers(SceneWork& work, entt::entity entity, Component& component);
    /// Subscribe a fill observer on a mesh source's model slot.
    void subscribeMeshSlotObserver(SceneWork& work, entt::entity entity, const AssetHandle<Model>& handle);
    void resolvePendingMeshes(SceneWork& work);
    void resolvePendingMaterials(SceneWork& work);
    void resolvePendingBillboards(SceneWork& work);
#ifdef BUILD_DEBUG
    void auditSlotSubscriptions(SceneWork& work);
#endif

    std::function<uint64_t()> _getHostTick;
    std::unordered_map<const entt::registry*, SceneWork> _sceneWork;
    DelegateHandle _componentAddedHandle   = INVALID_HANDLE;
    DelegateHandle _componentEditedHandle  = INVALID_HANDLE;
    DelegateHandle _componentRemovedHandle = INVALID_HANDLE;
    bool           bBusSubscribed          = false;
};

} // namespace ya
