#pragma once

// ============================================================================
// Environment lighting derived-GPU processing (skybox cubemap, irradiance,
// prefilter, terrain mesh). Owned by Render3D; the ECS ResourceResolveSystem
// keeps plain AssetRef / mesh / material resolution only.
//
// Driven as an ISystem by the Host, with render, offscreen queue and active
// scene injected through setters (no Host/App access).
// ============================================================================

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "RHI/Core/OffscreenJob.h"
#include "RHI/Core/ImageResourceRef.h"
#include "Host/Utility/OffscreenJobRunner.h"
#include "Render3D/Pipelines/CubeMap2PBRIrradianceMap.h"
#include "Render3D/Pipelines/CubeMap2PBRPrefilteredEnv.h"
#include "Render3D/Pipelines/EquidistantCylindrical2CubeMap.h"
#include "Resource/AssetManager.h"

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "entt/entt.hpp"

namespace ya
{

// Forward declarations
struct IRender;
struct PhongMaterialComponent;
struct PBRMaterialComponent;
struct RenderImage;
struct Scene;

struct SkyboxPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

struct TerrainDerivedResource
{
    stdptr<Mesh> mesh = nullptr;
    uint64_t     heightMapVersion = 0;
    uint64_t     lastUsedFrame    = 0;
};

struct SkyboxDerivedResource
{
    std::shared_ptr<RenderImage>                   cubemapRenderImage   = nullptr;
    stdptr<Texture>                                cubemapTexture       = nullptr;
    stdptr<Texture>                                sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    uint64_t                                       lastUsedFrame = 0;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return (cubemapRenderImage && cubemapRenderImage->isValid()) ||
               (cubemapTexture && cubemapTexture->getImageView());
    }
};

struct SkyboxRuntimeState
{
    uint64_t                                       authoringVersion     = 0;
    uint64_t                                       resultVersion        = 0;
    uint64_t                                       lastQueuedAuthoringVersion = 0;
    uint64_t                                       lastStartedAuthoringVersion = 0;
    uint64_t                                       lastCompletedAuthoringVersion = 0;
    std::string                                    lastDirtyReason;
    std::string                                    derivedKey;
    ESkyboxResolveState                            resolveState = ESkyboxResolveState::Dirty;
    std::shared_ptr<SkyboxDerivedResource>         boundResource;
    std::shared_ptr<RenderImage>                   cubemapRenderImage   = nullptr;
    stdptr<Texture>                                cubemapTexture       = nullptr;
    stdptr<Texture>                                sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    std::shared_ptr<SkyboxPendingBatchLoadState>   pendingBatchLoadState;
    std::shared_ptr<OffscreenJobState>             pendingOffscreenProcess;
    std::optional<TextureFuture>                   pendingCylindricalFuture;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return (cubemapRenderImage && cubemapRenderImage->isValid()) ||
               (cubemapTexture && cubemapTexture->getImageView());
    }
};

struct EnvironmentLightingPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

struct EnvironmentLightingDerivedResource
{
    static constexpr uint32_t                                 MAX_PREFILTER_PREVIEW_MIPS = 16;

    std::shared_ptr<RenderImage>                              cubemapRenderImage       = nullptr;
    stdptr<Texture>                                           cubemapTexture           = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            cubemapFacePreviewViews{};
    std::shared_ptr<RenderImage>                              irradianceRenderImage    = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            irradianceFacePreviewViews{};
    std::shared_ptr<RenderImage>                              prefilterRenderImage     = nullptr;
    std::array<std::array<stdptr<IImageView>, CubeFace_Count>, MAX_PREFILTER_PREVIEW_MIPS> prefilterMipFacePreviewViews{};
    uint32_t                                                  prefilterPreviewMipCount = 0;
    uint64_t                                                  lastUsedFrame            = 0;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return (cubemapRenderImage && cubemapRenderImage->isValid()) ||
               (cubemapTexture && cubemapTexture->getImageView());
    }

    [[nodiscard]] bool hasIrradianceMap() const
    {
        return irradianceRenderImage && irradianceRenderImage->isValid();
    }

    [[nodiscard]] bool hasPrefilterMap() const
    {
        return prefilterRenderImage && prefilterRenderImage->isValid();
    }
};

struct EnvironmentLightingRuntimeState
{
    static constexpr uint32_t                                 MAX_PREFILTER_PREVIEW_MIPS = 16;

    uint64_t                                                  authoringVersion             = 0;
    uint64_t                                                  resultVersion                = 0;
    uint64_t                                                  lastSceneSkyboxResultVersion = 0;
    bool                                                      bSceneSkyboxDependencyReady  = false;
    uint64_t                                                  lastQueuedAuthoringVersion   = 0;
    uint64_t                                                  lastStartedAuthoringVersion  = 0;
    uint64_t                                                  lastCompletedAuthoringVersion = 0;
    std::string                                               lastDirtyReason;
    std::string                                               derivedKey;
    EEnvironmentLightingSourceResolveState                     sourceState = EEnvironmentLightingSourceResolveState::Dirty;
    EEnvironmentLightingIrradianceResolveState                 irradianceState = EEnvironmentLightingIrradianceResolveState::Dirty;
    EEnvironmentLightingPrefilterResolveState                  prefilterState = EEnvironmentLightingPrefilterResolveState::Dirty;
    std::shared_ptr<EnvironmentLightingDerivedResource>       boundResource;
    std::shared_ptr<RenderImage>                              cubemapRenderImage           = nullptr;
    stdptr<Texture>                                           cubemapTexture               = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            cubemapFacePreviewViews{};
    std::shared_ptr<RenderImage>                              irradianceRenderImage        = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            irradianceFacePreviewViews{};
    std::shared_ptr<RenderImage>                              prefilterRenderImage         = nullptr;
    std::array<std::array<stdptr<IImageView>, CubeFace_Count>, MAX_PREFILTER_PREVIEW_MIPS> prefilterMipFacePreviewViews{};
    uint32_t                                                  prefilterPreviewMipCount     = 0;
    std::shared_ptr<EnvironmentLightingPendingBatchLoadState> pendingBatchLoad;
    std::shared_ptr<OffscreenJobState>                        pendingEnvironmentOffscreen;
    std::shared_ptr<OffscreenJobState>                        pendingIrradianceOffscreen;
    std::shared_ptr<OffscreenJobState>                        pendingPrefilterOffscreen;
    std::optional<TextureFuture>                              pendingCylindricalFuture;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return (cubemapRenderImage && cubemapRenderImage->isValid()) ||
               (cubemapTexture && cubemapTexture->getImageView());
    }

    [[nodiscard]] bool hasIrradianceMap() const
    {
        return irradianceRenderImage && irradianceRenderImage->isValid();
    }

    [[nodiscard]] bool hasPrefilterMap() const
    {
        return prefilterRenderImage && prefilterRenderImage->isValid();
    }
};

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
    AssetManager::TextureBatchMemoryHandle pendingHeightMapHandle = 0;
    uint64_t    lastBuiltHeightMapVersion  = 0;
    uint64_t    lastQueuedAuthoringVersion    = 0;
    uint64_t    lastStartedAuthoringVersion   = 0;
    uint64_t    lastCompletedAuthoringVersion = 0;
    std::string lastDirtyReason;
    std::string currentDerivedKey;
    std::shared_ptr<TerrainDerivedResource> boundResource;
};

// ── Read-only preview types for tooling and debug rendering ───────────

struct SkyboxPreviewInfo
{
    std::shared_ptr<RenderImage>             cubemapRenderImage   = nullptr;
    std::shared_ptr<IImage>                  cubemapImage         = nullptr;
    Texture*                                sourcePreviewTexture = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    bool                                    bHasRenderableCubemap = false;
};

struct EnvironmentLightingPreviewInfo
{
    std::shared_ptr<RenderImage>             cubemapRenderImage        = nullptr;
    std::shared_ptr<IImage>                  cubemapImage              = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    std::shared_ptr<RenderImage>             irradianceRenderImage     = nullptr;
    std::shared_ptr<IImage>                  irradianceImage           = nullptr;
    std::array<IImageView*, CubeFace_Count> irradianceFaceViews{};
    std::shared_ptr<RenderImage>             prefilterRenderImage      = nullptr;
    std::shared_ptr<IImage>                  prefilterImage            = nullptr;
    std::array<std::array<IImageView*, CubeFace_Count>, EnvironmentLightingRuntimeState::MAX_PREFILTER_PREVIEW_MIPS> prefilterMipFaceViews{};
    uint32_t                                prefilterMipCount     = 0;
    bool                                    bHasRenderableCubemap = false;
    bool                                    bHasIrradianceMap     = false;
    bool                                    bHasPrefilterMap      = false;
};

struct EnvironmentLightingSceneResources
{
    ImageResourceRef           cubemap{};
    ImageResourceRef           irradiance{};
    ImageResourceRef           prefilter{};
    std::shared_ptr<RenderImage> brdfLut = nullptr;

    [[nodiscard]] bool isComplete() const
    {
        return cubemap.isValid() && irradiance.isValid() && prefilter.isValid() &&
               brdfLut && brdfLut->getImageView();
    }
};


struct YA_RENDER_3D_API EnvironmentLightingProcessor : public ISystem
{
  private:
    IRender*                                                         _render = nullptr;
    OffscreenJobQueueService                                         _offscreenQueueService{};
    std::function<Scene*()>                                          _getActiveScene;
    EquidistantCylindrical2CubeMap                                    _equidistantCylindrical2CubeMap;
    CubeMap2PBRIrradianceMap                                          _cubeMap2IrradianceMap;
    CubeMap2PBRPrefilteredEnv                                         _cubeMap2PrefilterPipeline;
    Scene*                                                            _pendingStateScene = nullptr;
    std::unordered_map<entt::entity, TerrainRuntimeState>             _terrainStates;
    std::unordered_map<entt::entity, SkyboxRuntimeState>              _skyboxStates;
    std::unordered_map<entt::entity, EnvironmentLightingRuntimeState> _environmentStates;
    std::unordered_map<std::string, std::shared_ptr<TerrainDerivedResource>> _terrainDerivedResources;
    std::unordered_map<std::string, std::shared_ptr<SkyboxDerivedResource>> _skyboxDerivedResources;
    std::unordered_map<std::string, std::shared_ptr<EnvironmentLightingDerivedResource>> _environmentDerivedResources;
    std::deque<entt::entity>                                          _dirtyTerrainQueue;
    std::deque<entt::entity>                                          _dirtySkyboxQueue;
    std::deque<entt::entity>                                          _dirtyEnvironmentQueue;
    std::unordered_set<entt::entity>                                  _dirtyTerrainSet;
    std::unordered_set<entt::entity>                                  _dirtySkyboxSet;
    std::unordered_set<entt::entity>                                  _dirtyEnvironmentSet;
    std::unordered_set<entt::entity>                                  _activeTerrain;
    std::unordered_set<entt::entity>                                  _activeSkybox;
    std::unordered_set<entt::entity>                                  _activeEnvironment;
    std::unordered_set<entt::entity>                                  _sceneSkyboxEnvironmentDependents;
    uint64_t                                                          _nextResolveAuditFrame = 0;

    void seedSceneResolveWork(Scene* scene);
    void touchDerivedResourceUsage();
    void gcDerivedResources(uint64_t currentFrame);
    void auditResolveWork(Scene* scene);
    void markAllSceneSkyboxEnvironmentDependentsDirty(const char* reason);
    void clearSceneResolveWork();
    void clearAllResolveState();
    void cleanupTerrainState(entt::entity entity);
    void cleanupSkyboxState(entt::entity entity);
    void cleanupEnvironmentLightingState(entt::entity entity);
    [[nodiscard]] bool isTerrainQueuedOrActive(entt::entity entity) const;
    [[nodiscard]] bool isSkyboxQueuedOrActive(entt::entity entity) const;
    [[nodiscard]] bool isEnvironmentQueuedOrActive(entt::entity entity) const;

  public:
    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }
    void setOffscreenJobQueueService(OffscreenJobQueueService queueService) { _offscreenQueueService = std::move(queueService); }
    [[nodiscard]] const OffscreenJobQueueService& getOffscreenJobQueueService() const { return _offscreenQueueService; }
    void setActiveSceneProvider(std::function<Scene*()> provider) { _getActiveScene = std::move(provider); }
    void init() override;

    /// Resolve all pending skybox / environment / terrain derived GPU work.
    void onUpdate(float dt) override;

    void shutdown() override;

    void clearPendingResolveStates();
    void markTerrainDirty(entt::entity entity, const char* reason, uint64_t rebuildNotBeforeFrame = 0);
    void markSkyboxDirty(entt::entity entity, const char* reason);
    void markEnvironmentLightingDirty(entt::entity entity, const char* reason);
    void resolvePendingTerrain(Scene* scene);
    void resolvePendingSkybox(Scene* scene);
    void resolvePendingEnvironmentLighting(Scene* scene);

    static constexpr uint64_t DERIVED_RESOURCE_GC_DELAY_FRAMES = 300;

    // Pipeline accessors — used by step functions to bind concrete execute lambdas
    EquidistantCylindrical2CubeMap& getCylindrical2CubePipeline() { return _equidistantCylindrical2CubeMap; }
    CubeMap2PBRIrradianceMap&       getCube2IrradiancePipeline() { return _cubeMap2IrradianceMap; }
    CubeMap2PBRPrefilteredEnv&      getCube2PrefilterPipeline() { return _cubeMap2PrefilterPipeline; }
    [[nodiscard]] Mesh*                   getTerrainMesh(entt::entity entity) const;
    [[nodiscard]] const TerrainRuntimeState* findTerrainState(entt::entity entity) const;
    [[nodiscard]] ESkyboxResolveState getSkyboxResolveState(entt::entity entity) const;
    [[nodiscard]] bool isSkyboxLoading(entt::entity entity) const;
    [[nodiscard]] const SkyboxRuntimeState* findSkyboxState(entt::entity entity) const;
    [[nodiscard]] EEnvironmentLightingSourceResolveState getEnvironmentSourceState(entt::entity entity) const;
    [[nodiscard]] EEnvironmentLightingIrradianceResolveState getEnvironmentIrradianceState(entt::entity entity) const;
    [[nodiscard]] EEnvironmentLightingPrefilterResolveState getEnvironmentPrefilterState(entt::entity entity) const;
    [[nodiscard]] bool isEnvironmentLightingLoading(entt::entity entity) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findEnvironmentLightingState(entt::entity entity) const;

    // ── Internal state queries (used by rendering) ────────────────────
    [[nodiscard]] const SkyboxRuntimeState*              findFirstSceneSkyboxState(Scene* scene) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findFirstSceneEnvironmentLightingState(Scene* scene) const;
    [[nodiscard]] ImageResourceRef resolveSceneSkyboxResource(Scene* scene) const;
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene) const;

    // ── Read-only preview queries (tooling and debug) ─────────────────
    [[nodiscard]] SkyboxPreviewInfo              getSkyboxPreview(entt::entity entity) const;
    [[nodiscard]] EnvironmentLightingPreviewInfo getEnvironmentLightingPreview(entt::entity entity) const;
};

} // namespace ya
