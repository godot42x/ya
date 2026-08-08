#pragma once

#include "Foundation/Core/Base.h"
#include "Foundation/RHI/Core/DescriptorSet.h"
#include "Framework/Game/Gameplay/ECS/System/ResourceResolveSystem.h"
#include "Framework/Game/Render/Render3D/Pipelines/PBRGenerateBrdfLUT.h"

namespace ya
{

struct App;
struct Scene;
struct Texture;
struct RenderImage;
struct Sampler;
struct IRender;
struct IImageView;

struct RenderSharedResourceProvider
{
    IRender* _render = nullptr;
    App*     _app    = nullptr;

    struct SkyboxResources
    {
        stdptr<IDescriptorPool>      dsp               = nullptr;
        stdptr<IDescriptorSetLayout> dsl               = nullptr;
        stdptr<Texture>              fallbackTexture   = nullptr;
        DescriptorSetHandle          fallbackDS        = nullptr;
        DescriptorSetHandle          sceneDS           = nullptr;
        ImageViewHandle              boundSceneImageView = nullptr;
    };

    struct EnvironmentLightingResources
    {
        stdptr<IDescriptorPool>      dsp                       = nullptr;
        stdptr<IDescriptorSetLayout> dsl                       = nullptr;
        DescriptorSetHandle          fallbackDS                = nullptr;
        DescriptorSetHandle          sceneDS                   = nullptr;
        stdptr<Texture>              fallbackIrradianceTexture = nullptr;
        stdptr<Texture>              fallbackPrefilterTexture  = nullptr;
        ImageViewHandle              boundCubemapImageView     = nullptr;
        ImageViewHandle              boundIrradianceImageView  = nullptr;
        ImageViewHandle              boundPrefilterImageView   = nullptr;
        ImageViewHandle              boundBrdfLutImageView     = nullptr;
    };

    struct SharedResources
    {
        stdptr<RenderImage> pbrLUT = nullptr;
    };

    SkyboxResources              _skybox{};
    EnvironmentLightingResources _environmentLighting{};
    SharedResources              _sharedResources{};
    stdptr<Sampler>              _cubemapSampler = nullptr;
    PBRGenerateBrdfLUT           _pbrGenerateBrdfLUT{};

    void init(IRender* render, App* app);
    void shutdown();

    void resetSkyboxPool();
    void resetEnvironmentLightingPool();

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _skybox.dsp; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _skybox.dsl; }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _cubemapSampler.get(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _skybox.fallbackDS; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _environmentLighting.dsl; }
    [[nodiscard]] stdptr<RenderImage>          getBrdfLutTextureShared() const { return _sharedResources.pbrLUT; }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const;

  private:
    void                   initSharedPipelineResources();
    void                   initSkyboxResources();
    void                   initEnvironmentLightingResources();
    void                   releaseRenderOwnedResources();
    void                   updateSkyboxDescriptorSet(DescriptorSetHandle ds, IImageView* imageView);
    void                   updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds,
                                                                  IImageView*         cubemapImageView,
                                                                  IImageView*         irradianceImageView,
                                                                  IImageView*         prefilterImageView,
                                                                  IImageView*         brdfLutImageView);
};

} // namespace ya
