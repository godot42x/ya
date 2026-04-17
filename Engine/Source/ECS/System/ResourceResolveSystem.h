
#pragma once

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render/Core/OffscreenJob.h"
#include "Render/Pipelines/CubeMap2PBRIrradianceMap.h"
#include "Render/Pipelines/CubeMap2PBRPrefilteredEnv.h"
#include "Render/Pipelines/EquidistantCylindrical2CubeMap.h"
#include "Resource/AssetManager.h"

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

struct SkyboxRuntimeState
{
    uint64_t                                       authoringVersion     = 0;
    uint64_t                                       resultVersion        = 0;
    stdptr<Texture>                                cubemapTexture       = nullptr;
    stdptr<Texture>                                sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    std::shared_ptr<SkyboxPendingBatchLoadState>   pendingBatchLoadState;
    std::shared_ptr<OffscreenJobState>             pendingOffscreenProcess;
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
    stdptr<Texture>                                           prefilterTexture             = nullptr;
    std::shared_ptr<EnvironmentLightingPendingBatchLoadState> pendingBatchLoad;
    std::shared_ptr<OffscreenJobState>                        pendingEnvironmentOffscreen;
    std::shared_ptr<OffscreenJobState>                        pendingIrradianceOffscreen;
    std::shared_ptr<OffscreenJobState>                        pendingPrefilterOffscreen;
    std::optional<TextureFuture>                              pendingCylindricalFuture;

    [[nodiscard]] bool hasRenderableCubemap() const
    {
        return cubemapTexture && cubemapTexture->getImageView();
    }

    [[nodiscard]] bool hasIrradianceMap() const
    {
        return irradianceTexture && irradianceTexture->getImageView();
    }

    [[nodiscard]] bool hasPrefilterMap() const
    {
        return prefilterTexture && prefilterTexture->getImageView();
    }
};

// ── Read-only preview types for Editor / debug rendering ──────────────

struct SkyboxPreviewInfo
{
    Texture*                                sourcePreviewTexture = nullptr;
    Texture*                                cubemapTexture       = nullptr;
    std::array<IImageView*, CubeFace_Count> cubemapFaceViews{};
    bool                                    bHasRenderableCubemap = false;
};

struct EnvironmentLightingPreviewInfo
{
    Texture* cubemapTexture        = nullptr;
    Texture* irradianceTexture     = nullptr;
    Texture* prefilterTexture      = nullptr;
    bool     bHasRenderableCubemap = false;
    bool     bHasIrradianceMap     = false;
    bool     bHasPrefilterMap      = false;
};


struct ResourceResolveSystem : public ISystem
{

  private:
    EquidistantCylindrical2CubeMap                                    _equidistantCylindrical2CubeMap;
    CubeMap2PBRIrradianceMap                                          _cubeMap2IrradianceMap;
    CubeMap2PBRPrefilteredEnv                                         _cubeMap2PrefilterPipeline;
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

    // Pipeline accessors — used by step functions to bind concrete execute lambdas
    EquidistantCylindrical2CubeMap& getCylindrical2CubePipeline() { return _equidistantCylindrical2CubeMap; }
    CubeMap2PBRIrradianceMap&       getCube2IrradiancePipeline() { return _cubeMap2IrradianceMap; }
    CubeMap2PBRPrefilteredEnv&      getCube2PrefilterPipeline() { return _cubeMap2PrefilterPipeline; }

    // ── Internal state queries (used by rendering) ────────────────────
    [[nodiscard]] const SkyboxRuntimeState*              findSkyboxState(entt::entity entity) const;
    [[nodiscard]] const SkyboxRuntimeState*              findFirstSceneSkyboxState(Scene* scene) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findEnvironmentLightingState(entt::entity entity) const;
    [[nodiscard]] const EnvironmentLightingRuntimeState* findFirstSceneEnvironmentLightingState(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneSkyboxTexture(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneEnvironmentCubemapTexture(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneEnvironmentIrradianceTexture(Scene* scene) const;
    [[nodiscard]] Texture*                               findSceneEnvironmentPrefilterTexture(Scene* scene) const;

    // Shared-ownership texture queries (for snapshot lifetime safety)
    [[nodiscard]] stdptr<Texture> findSceneSkyboxTextureShared(Scene* scene) const;
    [[nodiscard]] stdptr<Texture> findSceneEnvironmentIrradianceTextureShared(Scene* scene) const;

    // ── Read-only preview queries (Editor / debug) ────────────────────
    [[nodiscard]] SkyboxPreviewInfo              getSkyboxPreview(entt::entity entity) const;
    [[nodiscard]] EnvironmentLightingPreviewInfo getEnvironmentLightingPreview(entt::entity entity) const;
};

} // namespace ya
