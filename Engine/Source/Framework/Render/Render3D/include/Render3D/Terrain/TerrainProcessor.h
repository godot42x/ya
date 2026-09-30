#pragma once

#include "Core/Api.h"
#include "Core/Common/AssetSlot.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"
#include "Core/TypeIndex.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "entt/entt.hpp"

namespace ya
{

struct IRender;
struct Scene;
struct Mesh;
struct Texture;

/// Derived CPU+GPU terrain result (height-map decode + mesh build).
struct TerrainDerivedResource
{
    std::shared_ptr<Mesh> mesh             = nullptr;
    uint64_t              heightMapVersion = 0;
    uint64_t              lastUsedTick     = 0;
};

/// Per-entity terrain resolve state (height-map load + mesh rebuild).
struct TerrainRuntimeState
{
    enum class EResolveState : uint8_t
    {
        Empty = 0,
        Dirty,
        LoadingHeightMap,
        Ready,
        Failed,
    };

    EResolveState state = EResolveState::Empty;
    uint64_t      pendingHeightMapHandle        = 0;
    uint64_t      lastBuiltHeightMapVersion     = 0;
    uint64_t      lastQueuedAuthoringVersion    = 0;
    uint64_t      lastStartedAuthoringVersion   = 0;
    uint64_t      lastCompletedAuthoringVersion = 0;
    std::string   lastDirtyReason;
    std::string   currentDerivedKey;
    std::shared_ptr<TerrainDerivedResource> boundResource;
};

/**
 * @brief Terrain derived-resource processor (height-map decode + mesh build).
 *
 * Discovery is the scene edit funnel (SceneBus add / edit / remove) plus one
 * seed pass for components that predate the first prepare. A height-map
 * decode completes through its batch callback, which only enqueues; the
 * processor holds the height-map slot subscription so a reload enqueues the
 * same way. Rebuilds requested for a future tick sit in a min-heap and the
 * tick only inspects the top. Steady state does not scan component views,
 * audit, or re-pump in-flight loads.
 *
 * The Scene is an argument everywhere, never a lookup: the tick names the
 * Scenes it renders and each one keeps its own resolve work below, so
 * preparing one Scene can neither drop nor overwrite another's. The derived
 * cache stays shared because it is keyed by what the resource was built from,
 * not by which Scene asked for it.
 */
class YA_RENDER_3D_API TerrainProcessor : public ISystem
{
  public:
    /// One Scene's terrain resolve work.
    struct SceneWork
    {
        /// Slot subscription and in-flight batch identity for one entity.
        /// The handle keeps the observed slot alive until the token dies.
        struct EntityTrack
        {
            AssetHandle<Texture>  heightMapHandle;
            AssetObservers::Token heightMapToken;
            uint64_t              scheduleSerial         = 0;
            uint64_t              batchSerial            = 0;
            uint64_t              inflightBatchHandle    = 0;
            uint64_t              startedResourceVersion = 0;
            /// Host tick this rebuild becomes eligible. Slot updates before
            /// it are ignored so a debounce cannot be bypassed.
            uint64_t              scheduledTick          = 0;
        };

        /// Min-heap entry. A newer schedule for the same entity bumps
        /// scheduleSerial; popped entries whose serial does not match are
        /// stale and dropped.
        struct DeferredRebuild
        {
            uint64_t     tick   = 0;
            entt::entity entity = entt::null;
            uint64_t     serial = 0;

            [[nodiscard]] bool operator>(const DeferredRebuild& rhs) const { return tick > rhs.tick; }
        };

        Scene*                                                scene = nullptr;
        std::unordered_map<entt::entity, TerrainRuntimeState> states;
        std::unordered_map<entt::entity, EntityTrack>         tracks;
        std::deque<entt::entity>                              dirtyQueue;
        std::unordered_set<entt::entity>                      dirtySet;
        std::priority_queue<DeferredRebuild,
                            std::vector<DeferredRebuild>,
                            std::greater<DeferredRebuild>>
            debounce;
        bool                                                  bSeeded = false;
    };

    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }
    void setHostTickProvider(std::function<uint64_t()> provider) { _getHostTick = std::move(provider); }

    void init() override;
    void shutdown() override;

    /// Prepares the derived state of exactly these Scenes, in order. A Scene
    /// this call does not name has its work dropped -- the same reconcile the
    /// pipelines do with the Views a tick stops declaring.
    void prepareScenes(std::span<Scene* const> scenes, float dt);
    void clearPendingResolveStates();

    static constexpr uint64_t DERIVED_RESOURCE_GC_DELAY_TICKS = 300;

    /// The scene argument names whose terrain is asked about; there is no
    /// current-Scene fallback when it is not the Scene the caller meant.
    [[nodiscard]] Mesh*                      getTerrainMesh(const Scene& scene, entt::entity entity) const;
    [[nodiscard]] const TerrainRuntimeState* findTerrainState(const Scene& scene, entt::entity entity) const;

  private:
    SceneWork&                     ensureWork(Scene& scene);
    [[nodiscard]] const SceneWork* findWork(const Scene& scene) const;
    [[nodiscard]] SceneWork*       findWorkByRegistry(const entt::registry* registry);
    void                           dropScenesAbsentFrom(std::span<Scene* const> scenes);
    void                           dropWork(SceneWork& work);

    void seedSceneResolveWork(SceneWork& work);
    void promoteDueRebuilds(SceneWork& work);
    void gcDerivedResources(uint64_t currentTick);
    void cleanupTerrainState(SceneWork& work, entt::entity entity);
    void cancelInflightBatch(SceneWork& work, entt::entity entity);
    void scheduleRebuild(SceneWork& work, entt::entity entity, uint64_t rebuildNotBeforeTick, const char* reason);
    void holdHeightMapSlot(SceneWork& work, entt::entity entity, const AssetHandle<Texture>& handle);
    void enqueueTerrain(SceneWork& work, entt::entity entity);
    void enqueueFromHeightMapSlot(const entt::registry* registry, entt::entity entity);
    void onHeightMapBatch(const entt::registry* registry, entt::entity entity, uint64_t serial, uint64_t handle);
    void resolvePendingTerrain(SceneWork& work);
    [[nodiscard]] uint64_t currentHostTick() const { return _getHostTick ? _getHostTick() : 0; }

    void onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);

    IRender*                  _render = nullptr;
    std::function<uint64_t()> _getHostTick;
    /// One entry per Scene the tick renders; a Scene that leaves the tick
    /// leaves this map.
    std::unordered_map<const Scene*, SceneWork> _sceneWork;
    std::unordered_map<std::string, std::shared_ptr<TerrainDerivedResource>> _terrainDerivedResources;

    DelegateHandle _componentAddedHandle   = INVALID_HANDLE;
    DelegateHandle _componentEditedHandle  = INVALID_HANDLE;
    DelegateHandle _componentRemovedHandle = INVALID_HANDLE;
    bool           bBusSubscribed          = false;
};

} // namespace ya
