#pragma once

#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Pipelines/PBRGenerateBrdfLUT.h"

namespace ya
{

struct App;
struct Scene;
struct Texture;
struct RenderImage;
struct Sampler;
struct IRender;

struct RenderSharedResourceProvider
{
    struct EnvironmentLightingTextureSet
    {
        Texture*     cubemapTexture    = nullptr;
        Texture*     irradianceTexture = nullptr;
        Texture*     prefilterTexture  = nullptr;
        RenderImage* brdfLutTexture    = nullptr;

        [[nodiscard]] bool isComplete() const
        {
            return cubemapTexture && irradianceTexture && prefilterTexture && brdfLutTexture;
        }
    };

    IRender* _render = nullptr;
    App*     _app    = nullptr;

    struct SkyboxResources
    {
        stdptr<IDescriptorPool>      dsp               = nullptr;
        stdptr<IDescriptorSetLayout> dsl               = nullptr;
        stdptr<Texture>              fallbackTexture   = nullptr;
        DescriptorSetHandle          fallbackDS        = nullptr;
        DescriptorSetHandle          sceneDS           = nullptr;
        Texture*                     boundSceneTexture = nullptr;
    };

    struct EnvironmentLightingResources
    {
        stdptr<IDescriptorPool>      dsp                       = nullptr;
        stdptr<IDescriptorSetLayout> dsl                       = nullptr;
        DescriptorSetHandle          fallbackDS                = nullptr;
        DescriptorSetHandle          sceneDS                   = nullptr;
        stdptr<Texture>              fallbackIrradianceTexture = nullptr;
        stdptr<Texture>              fallbackPrefilterTexture  = nullptr;
        Texture*                     boundCubemapTexture       = nullptr;
        Texture*                     boundIrradianceTexture    = nullptr;
        Texture*                     boundPrefilterTexture     = nullptr;
        RenderImage*                 boundBrdfLutTexture       = nullptr;
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
    [[nodiscard]] RenderImage*                 getBrdfLutTexture() const { return _sharedResources.pbrLUT.get(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] EnvironmentLightingTextureSet resolveSceneEnvironmentLightingTextures(Scene* scene = nullptr) const;

  private:
    void                   initSharedPipelineResources();
    void                   initSkyboxResources();
    void                   initEnvironmentLightingResources();
    void                   releaseRenderOwnedResources();
    void                   updateSkyboxDescriptorSet(DescriptorSetHandle ds, Texture* texture);
    void                   updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds, Texture* cubemapTexture, Texture* irradianceTexture, Texture* prefilterTexture, RenderImage* brdfLutTexture);
    [[nodiscard]] Texture* findSceneSkyboxTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentCubemapTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentIrradianceTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentPrefilterTexture(Scene* scene) const;
};

} // namespace ya
