#pragma once

// ============================================================================
// Environment lighting derived-GPU processing (skybox cubemap, irradiance,
// prefilter, terrain mesh). Owned by Render3D; the ECS GameplayResourceBinding
// keeps plain AssetRef / mesh / material resolution only.
//
// Driven by the Host one Scene at a time: render and offscreen queue are
// injected through setters (no Host/App access), and the Scene is a
// parameter of every entry that needs one -- there is no active-Scene lookup.
// ============================================================================

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "RHI/Core/OffscreenJob.h"
#include "RHI/Core/ImageResource.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"
#include "Render3D/Pipelines/CubeMap2PBRIrradianceMap.h"
#include "Render3D/Pipelines/CubeMap2PBRPrefilteredEnv.h"
#include "Render3D/Pipelines/EquidistantCylindrical2CubeMap.h"
#include "Resource/AssetManager.h"

#include <deque>
#include <functional>
#include <span>
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
struct Scene;

struct SkyboxPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

struct SkyboxDerivedResource
{
    std::shared_ptr<RenderTexture>                 cubemapRenderImage   = nullptr;
    stdptr<Texture>                                cubemapTexture       = nullptr;
    stdptr<Texture>                                sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    uint64_t                                       lastUsedTick = 0;

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
    std::shared_ptr<RenderTexture>                 cubemapRenderImage   = nullptr;
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

    std::shared_ptr<RenderTexture>                            cubemapRenderImage       = nullptr;
    stdptr<Texture>                                           cubemapTexture           = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            cubemapFacePreviewViews{};
    std::shared_ptr<RenderTexture>                            irradianceRenderImage    = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            irradianceFacePreviewViews{};
    std::shared_ptr<RenderTexture>                            prefilterRenderImage     = nullptr;
    std::array<std::array<stdptr<IImageView>, CubeFace_Count>, MAX_PREFILTER_PREVIEW_MIPS> prefilterMipFacePreviewViews{};
    uint32_t                                                  prefilterPreviewMipCount = 0;
    uint64_t                                                  lastUsedTick            = 0;

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
    std::shared_ptr<RenderTexture>                            cubemapRenderImage           = nullptr;
    stdptr<Texture>                                           cubemapTexture               = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            cubemapFacePreviewViews{};
    std::shared_ptr<RenderTexture>                            irradianceRenderImage        = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count>            irradianceFacePreviewViews{};
    std::shared_ptr<RenderTexture>                            prefilterRenderImage         = nullptr;
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

// ── Read-only preview types for tooling and debug rendering ───────────

struct SkyboxPreviewInfo
{
    std::shared_ptr<RenderTexture>           cubemapRenderImage   = nullptr;
    std::shared_ptr<IImage>                  cubemapImage         = nullptr;
    Texture*                                sourcePreviewTexture = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    bool                                    bHasRenderableCubemap = false;
};

struct EnvironmentLightingPreviewInfo
{
    std::shared_ptr<RenderTexture>           cubemapRenderImage        = nullptr;
    std::shared_ptr<IImage>                  cubemapImage              = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    std::shared_ptr<RenderTexture>           irradianceRenderImage     = nullptr;
    std::shared_ptr<IImage>                  irradianceImage           = nullptr;
    std::array<IImageView*, CubeFace_Count> irradianceFaceViews{};
    std::shared_ptr<RenderTexture>           prefilterRenderImage      = nullptr;
    std::shared_ptr<IImage>                  prefilterImage            = nullptr;
    std::array<std::array<IImageView*, CubeFace_Count>, EnvironmentLightingRuntimeState::MAX_PREFILTER_PREVIEW_MIPS> prefilterMipFaceViews{};
    uint32_t                                prefilterMipCount     = 0;
    bool                                    bHasRenderableCubemap = false;
    bool                                    bHasIrradianceMap     = false;
    bool                                    bHasPrefilterMap      = false;
};

struct YA_RENDER_3D_API EnvironmentLightingProcessor : public ISystem
{
  public:
    /// One Scene's skybox / environment-lighting resolve work.
    ///
    /// A SceneWork *is* one Scene's state, so the entity-scoped queries below
    /// are unambiguous without asking which Scene is current.
    struct SceneWork
    {
        Scene*                                                            scene = nullptr;
        std::unordered_map<entt::entity, SkyboxRuntimeState>              skyboxStates;
        std::unordered_map<entt::entity, EnvironmentLightingRuntimeState> environmentStates;
        std::deque<entt::entity>                                          dirtySkyboxQueue;
        std::deque<entt::entity>                                          dirtyEnvironmentQueue;
        std::unordered_set<entt::entity>                                  dirtySkyboxSet;
        std::unordered_set<entt::entity>                                  dirtyEnvironmentSet;
        std::unordered_set<entt::entity>                                  activeSkybox;
        std::unordered_set<entt::entity>                                  activeEnvironment;
        std::unordered_set<entt::entity>                                  sceneSkyboxEnvironmentDependents;
        uint64_t                                                          nextResolveAuditTick = 0;
        bool                                                              bSeeded              = false;

        [[nodiscard]] ESkyboxResolveState getSkyboxResolveState(entt::entity entity) const;
        [[nodiscard]] bool                isSkyboxLoading(entt::entity entity) const;
        [[nodiscard]] const SkyboxRuntimeState* findSkyboxState(entt::entity entity) const;
        /// First entity of this Scene whose derived cubemap is ready to be
        /// used as a source for this Scene's environment lighting.
        [[nodiscard]] const SkyboxRuntimeState* findFirstReadySkyboxState() const;
        [[nodiscard]] EEnvironmentLightingSourceResolveState getEnvironmentSourceState(entt::entity entity) const;
        [[nodiscard]] EEnvironmentLightingIrradianceResolveState getEnvironmentIrradianceState(entt::entity entity) const;
        [[nodiscard]] EEnvironmentLightingPrefilterResolveState getEnvironmentPrefilterState(entt::entity entity) const;
        [[nodiscard]] bool isEnvironmentLightingLoading(entt::entity entity) const;
        [[nodiscard]] const EnvironmentLightingRuntimeState* findEnvironmentLightingState(entt::entity entity) const;
        [[nodiscard]] const EnvironmentLightingRuntimeState* findFirstReadyEnvironmentLightingState() const;

        // ── Read-only preview queries (tooling and debug) ─────────────
        [[nodiscard]] SkyboxPreviewInfo              getSkyboxPreview(entt::entity entity) const;
        [[nodiscard]] EnvironmentLightingPreviewInfo getEnvironmentLightingPreview(entt::entity entity) const;
    };

  private:
    IRender*                                                         _render = nullptr;
    OffscreenJobQueueService                                         _offscreenQueueService{};
    std::function<uint64_t()>                                        _getHostTick;

    EquidistantCylindrical2CubeMap                                    _equidistantCylindrical2CubeMap;
    CubeMap2PBRIrradianceMap                                          _cubeMap2IrradianceMap;
    CubeMap2PBRPrefilteredEnv                                         _cubeMap2PrefilterPipeline;
    /// Shared by every Scene: the cache is keyed by what the resource was
    /// built from, not by which Scene asked for it.
    std::unordered_map<std::string, std::shared_ptr<SkyboxDerivedResource>> _skyboxDerivedResources;
    std::unordered_map<std::string, std::shared_ptr<EnvironmentLightingDerivedResource>> _environmentDerivedResources;

    /// One entry per Scene the tick renders; a Scene that leaves the tick
    /// leaves this map.
    std::unordered_map<const Scene*, SceneWork>                       _sceneWork;

    SceneWork&                     ensureWork(Scene& scene);
    [[nodiscard]] const SceneWork* findWork(const Scene& scene) const;
    void                           dropScenesAbsentFrom(std::span<Scene* const> scenes);
    void                           dropWork(SceneWork& work);

    void seedSceneResolveWork(SceneWork& work);
    void touchDerivedResourceUsage(SceneWork& work);
    void gcDerivedResources(uint64_t currentFrame);
    void sweepAuthoringDirty(SceneWork& work);
    void auditResolveWork(SceneWork& work);
    void markAllSceneSkyboxEnvironmentDependentsDirty(SceneWork& work, const char* reason);
    void markSkyboxDirty(SceneWork& work, entt::entity entity, const char* reason);
    void markEnvironmentLightingDirty(SceneWork& work, entt::entity entity, const char* reason);
    void resolvePendingSkybox(SceneWork& work);
    void resolvePendingEnvironmentLighting(SceneWork& work);
    void clearAllResolveState();
    void cleanupSkyboxState(SceneWork& work, entt::entity entity);
    void cleanupEnvironmentLightingState(SceneWork& work, entt::entity entity);
    [[nodiscard]] bool isSkyboxQueuedOrActive(const SceneWork& work, entt::entity entity) const;
    [[nodiscard]] bool isEnvironmentQueuedOrActive(const SceneWork& work, entt::entity entity) const;

  public:
    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }
    void setOffscreenJobQueueService(OffscreenJobQueueService queueService) { _offscreenQueueService = std::move(queueService); }
    [[nodiscard]] const OffscreenJobQueueService& getOffscreenJobQueueService() const { return _offscreenQueueService; }
    void setHostTickProvider(std::function<uint64_t()> provider) { _getHostTick = std::move(provider); }
    void init() override;

    /// Resolves the derived state of exactly these Scenes, in order. A Scene
    /// this call does not name has its work dropped -- the same reconcile the
    /// pipelines do with the Views a tick stops declaring.
    void prepareScenes(std::span<Scene* const> scenes, float dt);

    void shutdown() override;

    void clearPendingResolveStates();
    /// Invalidation entries: the entity belongs to the Scene named here.
    void markSkyboxDirty(Scene& scene, entt::entity entity, const char* reason);
    void markEnvironmentLightingDirty(Scene& scene, entt::entity entity, const char* reason);

    static constexpr uint64_t DERIVED_RESOURCE_GC_DELAY_TICKS = 300;

    // Pipeline accessors — used by step functions to bind concrete execute lambdas
    EquidistantCylindrical2CubeMap& getCylindrical2CubePipeline() { return _equidistantCylindrical2CubeMap; }
    CubeMap2PBRIrradianceMap&       getCube2IrradiancePipeline() { return _cubeMap2IrradianceMap; }
    CubeMap2PBRPrefilteredEnv&      getCube2PrefilterPipeline() { return _cubeMap2PrefilterPipeline; }

    /// The state of the Scene named here; nullptr when that Scene is not one
    /// this tick prepares. The loading queries are answered for the Scene
    /// asked about, never for whichever Scene happened to be prepared last.
    [[nodiscard]] const SceneWork* findSceneWork(const Scene& scene) const;
    [[nodiscard]] bool             isSkyboxLoading(const Scene& scene, entt::entity entity) const;
    [[nodiscard]] bool             isEnvironmentLightingLoading(const Scene& scene, entt::entity entity) const;

    // ── Internal state queries (used by rendering) ────────────────────
    [[nodiscard]] const SkyboxRuntimeState*              findFirstSceneSkyboxState(Scene* scene) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findFirstSceneEnvironmentLightingState(Scene* scene) const;
    [[nodiscard]] std::shared_ptr<ImageResource> resolveSceneSkyboxResource(Scene* scene) const;
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene) const;
};

} // namespace ya
