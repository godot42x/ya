
#pragma once

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render/Pipelines/CubeMap2IrradianceMap.h"
#include "Render/Pipelines/EquidistantCylindrical2CubeMap.h"
#include "Resource/AssetManager.h"

#include <functional>
#include <unordered_map>

namespace ya
{

// Forward declarations
struct MeshComponent;
struct PhongMaterialComponent;
struct PBRMaterialComponent;
struct Scene;

struct SkyboxPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

enum class EOffscreenPrecomputeOutputType
{
    Texture2D,
    Cubemap,
};

struct OffscreenPrecomputeOutputSpec
{
    EOffscreenPrecomputeOutputType outputType = EOffscreenPrecomputeOutputType::Cubemap;
    std::string                    label;
    uint32_t                       width      = 0;
    uint32_t                       height     = 0;
    EFormat::T                     format     = EFormat::Undefined;
    int                            mipLevels  = 1;
    uint32_t                       layerCount = 1;

    [[nodiscard]] bool isValid() const
    {
        return !label.empty() && width > 0 && height > 0 && format != EFormat::Undefined && mipLevels > 0 && layerCount > 0;
    }
};

struct OffscreenPrecomputeJobState
{
    // Self-describing execution: the lambda captures the concrete pipeline and its parameters.
    using ExecuteFn = std::function<bool(ICommandBuffer* cmdBuf, Texture* output)>;
    ExecuteFn executeFn;

    OffscreenPrecomputeOutputSpec outputSpec;
    stdptr<Texture>               sourceTexture  = nullptr;
    stdptr<Texture>               outputTexture  = nullptr;
    bool                          bTaskQueued    = false;
    bool                          bTaskFinished  = false;
    bool                          bTaskSucceeded = false;
    bool                          bCancelled     = false;

    [[nodiscard]] bool isReadyToQueue() const
    {
        return executeFn && sourceTexture && sourceTexture->getImageView() &&
               outputSpec.isValid() && !bTaskQueued;
    }
};

struct SkyboxRuntimeState
{
    uint64_t                                       authoringVersion     = 0;
    uint64_t                                       resultVersion        = 0;
    stdptr<Texture>                                cubemapTexture       = nullptr;
    stdptr<Texture>                                sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    std::shared_ptr<SkyboxPendingBatchLoadState>   pendingBatchLoad;
    std::shared_ptr<OffscreenPrecomputeJobState>   pendingOffscreenProcess;
    std::optional<TextureFuture>                   pendingCylindricalFuture;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return cubemapTexture && cubemapTexture->getImageView();
    }
};

struct EnvironmentLightingPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

struct EnvironmentLightingRuntimeState
{
    uint64_t                                                  authoringVersion             = 0;
    uint64_t                                                  resultVersion                = 0;
    uint64_t                                                  lastSceneSkyboxResultVersion = 0;
    stdptr<Texture>                                           cubemapTexture               = nullptr;
    stdptr<Texture>                                           irradianceTexture            = nullptr;
    std::shared_ptr<EnvironmentLightingPendingBatchLoadState> pendingBatchLoad;
    std::shared_ptr<OffscreenPrecomputeJobState>              pendingEnvironmentOffscreen;
    std::shared_ptr<OffscreenPrecomputeJobState>              pendingIrradianceOffscreen;
    std::optional<TextureFuture>                              pendingCylindricalFuture;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return cubemapTexture && cubemapTexture->getImageView();
    }

    [[nodiscard]] bool hasIrradianceMap() const
    {
        return irradianceTexture && irradianceTexture->getImageView();
    }
};

// ── Read-only preview types for Editor / debug rendering ──────────────

struct SkyboxPreviewInfo
{
    Texture*                                sourcePreviewTexture  = nullptr;
    Texture*                                cubemapTexture        = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    bool                                    bHasRenderableCubemap = false;
};

struct EnvironmentLightingPreviewInfo
{
    Texture* cubemapTexture        = nullptr;
    Texture* irradianceTexture     = nullptr;
    bool     bHasRenderableCubemap = false;
    bool     bHasIrradianceMap     = false;
};


struct ResourceResolveSystem : public ISystem
{

  private:
    EquidistantCylindrical2CubeMap                                    _equidistantCylindrical2CubeMap;
    CubeMap2IrradianceMap                                             _cubeMap2IrradianceMap;
    Scene*                                                            _pendingStateScene = nullptr;
    std::unordered_map<entt::entity, SkyboxRuntimeState>              _skyboxStates;
    std::unordered_map<entt::entity, EnvironmentLightingRuntimeState> _environmentStates;

  public:
    void init() override;

    /**
     * @brief Resolve all pending resources
     * Iterates through components and calls resolve() on unresolved ones
     */
    void onUpdate(float dt) override;

    void shutdown() override;


    void clearPendingResolveStates();
    void resolvePendingMeshes(Scene* scene);
    void resolvePendingMaterials(Scene* scene);
    void resolvePendingUI(Scene* scene);
    void resolvePendingBillboards(Scene* scene);

    void resolvePendingSkybox(Scene* scene);
    void resolvePendingEnvironmentLighting(Scene* scene);

    void queueOffscreenPrecomputeJob(const std::shared_ptr<OffscreenPrecomputeJobState>& job);

    // Pipeline accessors — used by step functions to bind concrete execute lambdas
    EquidistantCylindrical2CubeMap& getCylindrical2CubePipeline() { return _equidistantCylindrical2CubeMap; }
    CubeMap2IrradianceMap&          getCube2IrradiancePipeline() { return _cubeMap2IrradianceMap; }

    // ── Internal state queries (used by rendering) ────────────────────
    [[nodiscard]] const SkyboxRuntimeState*              findSkyboxState(entt::entity entity) const;
    [[nodiscard]] const SkyboxRuntimeState*              findFirstSceneSkyboxState(Scene* scene) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findEnvironmentLightingState(entt::entity entity) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findFirstSceneEnvironmentLightingState(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneSkyboxTexture(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneEnvironmentCubemapTexture(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneEnvironmentIrradianceTexture(Scene* scene) const;

    // Shared-ownership texture queries (for snapshot lifetime safety)
    [[nodiscard]] stdptr<Texture> findSceneSkyboxTextureShared(Scene* scene) const;
    [[nodiscard]] stdptr<Texture> findSceneEnvironmentIrradianceTextureShared(Scene* scene) const;

    // ── Read-only preview queries (Editor / debug) ────────────────────
    [[nodiscard]] SkyboxPreviewInfo              getSkyboxPreview(entt::entity entity) const;
    [[nodiscard]] EnvironmentLightingPreviewInfo getEnvironmentLightingPreview(entt::entity entity) const;
};

} // namespace ya
