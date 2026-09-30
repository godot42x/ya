
#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <deque>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;

/**
 * @brief Runtime resource resolution for the components a Scene already has.
 *
 * The Scene is an argument everywhere, never a lookup: the tick names the
 * Scenes it renders and each one keeps its own dirty queue, active set and
 * audit clock below, so resolving one Scene can neither drop nor overwrite
 * another's.
 */
struct YA_RENDER_3D_API GameplayResourceBinding : public ISystem
{
  public:
    /// One Scene's material resolve work.
    struct SceneWork
    {
        Scene*                           scene = nullptr;
        std::deque<entt::entity>         dirtyMaterialQueue;
        std::unordered_set<entt::entity> dirtyMaterialSet;
        std::unordered_set<entt::entity> activeMaterial;
        uint64_t                         nextMaterialAuditTick = 0;
        bool                             bSeeded               = false;
    };

    /// Frame counter for the periodic staleness audit; bound by the Host.
    void setHostTickProvider(std::function<uint64_t()> provider) { _getHostTick = std::move(provider); }

    /**
     * @brief Resolves the pending plain resources (mesh / material /
     * billboard) of exactly these Scenes. Texture refs bind their shared
     * asset slot when their path is set, so sprites, tilemaps and UI images
     * need no per-frame work here. Skybox / environment / terrain
     * derived GPU work lives in EnvironmentLightingProcessor and
     * TerrainProcessor. A Scene this call does not name has its work dropped.
     */
    void prepareScenes(std::span<Scene* const> scenes, float dt);

    void shutdown() override;

  private:
    /// How often the material staleness audit runs (ticks).
    static constexpr uint64_t MATERIAL_AUDIT_INTERVAL_TICKS = 30;

    SceneWork& ensureWork(Scene& scene);
    void       dropScenesAbsentFrom(std::span<Scene* const> scenes);
    void       dropWork(SceneWork& work);
    void       dropAllWork();
    void       seedSceneResolveWork(SceneWork& work);

    void auditMaterialWork(SceneWork& work);
    void cleanupMaterialState(SceneWork& work, entt::entity entity);
    [[nodiscard]] bool isMaterialQueuedOrActive(const SceneWork& work, entt::entity entity) const;
    void markMaterialDirty(SceneWork& work, entt::entity entity, const char* reason);
    void resolvePendingMeshes(Scene& scene);
    void resolvePendingMaterials(SceneWork& work);
    void resolvePendingBillboards(Scene& scene);

    std::function<uint64_t()>                   _getHostTick;
    std::unordered_map<const Scene*, SceneWork> _sceneWork;
};

} // namespace ya
