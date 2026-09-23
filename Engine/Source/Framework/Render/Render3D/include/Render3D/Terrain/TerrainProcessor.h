#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "entt/entt.hpp"

namespace ya
{

struct IRender;
struct Scene;
struct Mesh;

/// Derived CPU+GPU terrain result (height-map decode + mesh build).
struct TerrainDerivedResource
{
    std::shared_ptr<Mesh> mesh            = nullptr;
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
    uint64_t      pendingHeightMapHandle       = 0;
    uint64_t      lastBuiltHeightMapVersion    = 0;
    uint64_t      lastQueuedAuthoringVersion   = 0;
    uint64_t      lastStartedAuthoringVersion  = 0;
    uint64_t      lastCompletedAuthoringVersion = 0;
    std::string   lastDirtyReason;
    std::string   currentDerivedKey;
    std::shared_ptr<TerrainDerivedResource> boundResource;
};

/**
 * @brief Terrain derived-resource processor (height-map decode + mesh build).
 *
 * Independent from environment lighting: terrain owns its own dirty queue,
 * derived-resource cache and audit; both share only the generic job/cache
 * infrastructure. Render/scene/frame services are injected; this processor
 * never reaches Host.
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
        Scene*                                                scene = nullptr;
        std::unordered_map<entt::entity, TerrainRuntimeState> states;
        std::deque<entt::entity>                              dirtyQueue;
        std::unordered_set<entt::entity>                      dirtySet;
        std::unordered_set<entt::entity>                      active;
        uint64_t                                              nextResolveAuditTick = 0;
        bool                                                  bSeeded              = false;
    };

    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }
    void setHostTickProvider(std::function<uint64_t()> provider) { _getHostTick = std::move(provider); }

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
    void                           dropScenesAbsentFrom(std::span<Scene* const> scenes);
    void                           dropWork(SceneWork& work);

    void seedSceneResolveWork(SceneWork& work);
    void sweepAuthoringDirty(SceneWork& work);
    void auditResolveWork(SceneWork& work);
    void gcDerivedResources(uint64_t currentFrame);
    void cleanupTerrainState(SceneWork& work, entt::entity entity);
    void markTerrainDirty(SceneWork& work, entt::entity entity, const char* reason, uint64_t rebuildNotBeforeTick = 0);
    void resolvePendingTerrain(SceneWork& work);
    [[nodiscard]] bool isTerrainQueuedOrActive(const SceneWork& work, entt::entity entity) const;
    [[nodiscard]] uint64_t currentHostTick() const { return _getHostTick ? _getHostTick() : 0; }

    IRender*                  _render = nullptr;
    std::function<uint64_t()> _getHostTick;
    /// One entry per Scene the tick renders; a Scene that leaves the tick
    /// leaves this map.
    std::unordered_map<const Scene*, SceneWork> _sceneWork;
    std::unordered_map<std::string, std::shared_ptr<TerrainDerivedResource>> _terrainDerivedResources;
};

} // namespace ya
