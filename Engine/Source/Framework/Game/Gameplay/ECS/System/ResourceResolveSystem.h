
#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;

struct YA_GAMEPLAY_ECS_API ResourceResolveSystem : public ISystem
{
  private:
    std::function<Scene*()>  _getActiveScene;
    Scene*                   _pendingStateScene = nullptr;
    std::deque<entt::entity> _dirtyMaterialQueue;
    std::unordered_set<entt::entity> _dirtyMaterialSet;
    std::unordered_set<entt::entity> _activeMaterial;
    uint64_t                 _nextMaterialAuditFrame = 0;

    /// How often the material staleness audit runs (frames).
    static constexpr uint64_t MATERIAL_AUDIT_INTERVAL_FRAMES = 30;

    void auditMaterialWork(Scene* scene);
    void clearSceneResolveWork();
    void cleanupMaterialState(entt::entity entity);
    [[nodiscard]] bool isMaterialQueuedOrActive(entt::entity entity) const;

  public:
    void setActiveSceneProvider(std::function<Scene*()> provider) { _getActiveScene = std::move(provider); }
    void init() override;

    /**
     * @brief Resolve all pending plain resources (mesh / material / UI / billboard).
     * Skybox / environment / terrain derived GPU work moved to
     * EnvironmentLightingProcessor (Render3D).
     */
    void onUpdate(float dt) override;

    void shutdown() override;

    void markMaterialDirty(entt::entity entity, const char* reason);
    void resolvePendingMeshes(Scene* scene);
    void resolvePendingMaterials(Scene* scene);
    void resolvePendingUI(Scene* scene);
    void resolvePendingBillboards(Scene* scene);
};

} // namespace ya
