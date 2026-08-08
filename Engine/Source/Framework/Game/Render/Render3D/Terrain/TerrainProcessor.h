#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <deque>
#include <functional>
#include <memory>
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
    uint64_t              lastUsedFrame    = 0;
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
 */
class YA_RENDER_3D_API TerrainProcessor : public ISystem
{
  public:
    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }
    void setActiveSceneProvider(std::function<Scene*()> provider) { _getActiveScene = std::move(provider); }
    void setFrameIndexProvider(std::function<uint64_t()> provider) { _getFrameIndex = std::move(provider); }

    void init() override;
    void onUpdate(float dt) override;
    void shutdown() override;

    void clearPendingResolveStates();
    void markTerrainDirty(entt::entity entity, const char* reason, uint64_t rebuildNotBeforeFrame = 0);
    void resolvePendingTerrain(Scene* scene);

    static constexpr uint64_t DERIVED_RESOURCE_GC_DELAY_FRAMES = 300;

    [[nodiscard]] Mesh* getTerrainMesh(entt::entity entity) const;
    [[nodiscard]] const TerrainRuntimeState* findTerrainState(entt::entity entity) const;

  private:
    void seedSceneResolveWork(Scene* scene);
    void auditResolveWork(Scene* scene);
    void gcDerivedResources(uint64_t currentFrame);
    void clearSceneResolveWork();
    void cleanupTerrainState(entt::entity entity);
    [[nodiscard]] bool isTerrainQueuedOrActive(entt::entity entity) const;
    [[nodiscard]] uint64_t currentFrame() const { return _getFrameIndex ? _getFrameIndex() : 0; }

    IRender*                      _render = nullptr;
    std::function<Scene*()>       _getActiveScene;
    std::function<uint64_t()>     _getFrameIndex;
    Scene*                        _pendingStateScene = nullptr;
    std::unordered_map<entt::entity, TerrainRuntimeState> _terrainStates;
    std::unordered_map<std::string, std::shared_ptr<TerrainDerivedResource>> _terrainDerivedResources;
    std::deque<entt::entity>      _dirtyTerrainQueue;
    std::unordered_set<entt::entity> _dirtyTerrainSet;
    std::unordered_set<entt::entity> _activeTerrain;
    uint64_t                      _nextResolveAuditFrame = 0;
};

} // namespace ya
