
#pragma once

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render/Core/OffscreenJob.h"
#include "Render/Core/ImageResourceRef.h"
#include "Render/Pipelines/CubeMap2PBRIrradianceMap.h"
#include "Render/Pipelines/CubeMap2PBRPrefilteredEnv.h"
#include "Render/Pipelines/EquidistantCylindrical2CubeMap.h"
#include "Resource/AssetManager.h"

#include <unordered_map>

namespace ya
{

// Forward declarations
struct PhongMaterialComponent;
struct PBRMaterialComponent;
struct RenderImage;
struct Scene;

struct SkyboxPendingBatchLoadState
{
    AssetManager::TextureBatchMemoryHandle batchHandle = 0;
};

struct SkyboxRuntimeState
{
    uint64_t                                       authoringVersion     = 0;
    uint64_t                                       resultVersion        = 0;
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

struct EnvironmentLightingRuntimeState
{
    static constexpr uint32_t                                 MAX_PREFILTER_PREVIEW_MIPS = 16;

    uint64_t                                                  authoringVersion             = 0;
    uint64_t                                                  resultVersion                = 0;
    uint64_t                                                  lastSceneSkyboxResultVersion = 0;
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


struct ENGINE_API ResourceResolveSystem : public ISystem
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
    void resolvePendingTerrain(Scene* scene);
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
    [[nodiscard]] ImageResourceRef resolveSceneSkyboxResource(Scene* scene) const;
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene) const;

    // ── Read-only preview queries (tooling and debug) ─────────────────
    [[nodiscard]] SkyboxPreviewInfo              getSkyboxPreview(entt::entity entity) const;
    [[nodiscard]] EnvironmentLightingPreviewInfo getEnvironmentLightingPreview(entt::entity entity) const;
};

} // namespace ya
