#include "RenderSharedResourceProvider.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/RenderImage.h"

#include "App.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Render/Core/Sampler.h"

namespace ya
{

Texture* RenderSharedResourceProvider::findSceneSkyboxTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneSkyboxTexture(scene);
}

Texture* RenderSharedResourceProvider::findSceneEnvironmentCubemapTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentCubemapTexture(scene);
}

Texture* RenderSharedResourceProvider::findSceneEnvironmentIrradianceTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentIrradianceTexture(scene);
}

Texture* RenderSharedResourceProvider::findSceneEnvironmentPrefilterTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentPrefilterTexture(scene);
}

void RenderSharedResourceProvider::updateSkyboxDescriptorSet(DescriptorSetHandle ds, Texture* texture)
{
    if (!ds || !texture || !texture->getImageView() || !_cubemapSampler) {
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                ds,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(
                        texture->getImageView()->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});
}

void RenderSharedResourceProvider::updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds,
                                                                          Texture*            cubemapTexture,
                                                                          Texture*            irradianceTexture,
                                                                          Texture*            prefilterTexture,
                                                                          RenderImage*        brdfLutTexture)
{
    if (!ds || !cubemapTexture || !irradianceTexture || !prefilterTexture || !brdfLutTexture ||
        !cubemapTexture->getImageView() || !irradianceTexture->getImageView() ||
        !prefilterTexture->getImageView() || !brdfLutTexture->getImageView() || !_cubemapSampler) {
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                ds,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(
                        cubemapTexture->getImageView()->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
            IDescriptorSetHelper::genImageWrite(
                ds,
                1,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(
                        irradianceTexture->getImageView()->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
            IDescriptorSetHelper::genImageWrite(
                ds,
                2,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(
                        prefilterTexture->getImageView()->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
            IDescriptorSetHelper::genImageWrite(
                ds,
                3,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(
                        brdfLutTexture->getImageView()->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});
}

DescriptorSetHandle RenderSharedResourceProvider::getSceneSkyboxDescriptorSet(Scene* scene)
{
    if (!_skybox.sceneDS) {
        return _skybox.fallbackDS;
    }

    if (!scene && _app && _app->getSceneManager()) {
        scene = _app->getSceneManager()->getActiveScene();
    }

    auto* texture = findSceneSkyboxTexture(scene);
    if (!texture) {
        _skybox.boundSceneTexture = nullptr;
        return _skybox.fallbackDS;
    }

    if (texture != _skybox.boundSceneTexture) {
        updateSkyboxDescriptorSet(_skybox.sceneDS, texture);
        _skybox.boundSceneTexture = texture;
    }

    return _skybox.sceneDS;
}

DescriptorSetHandle RenderSharedResourceProvider::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    if (!_environmentLighting.sceneDS) {
        return _environmentLighting.fallbackDS;
    }

    auto resources = resolveSceneEnvironmentLightingTextures(scene);
    if (!resources.isComplete()) {
        return _environmentLighting.fallbackDS;
    }

    auto* cubemapTexture    = resources.cubemapTexture;
    auto* irradianceTexture = resources.irradianceTexture;
    auto* prefilterTexture  = resources.prefilterTexture;
    auto* brdfLutTexture    = resources.brdfLutTexture;

    if (cubemapTexture != _environmentLighting.boundCubemapTexture ||
        irradianceTexture != _environmentLighting.boundIrradianceTexture ||
        prefilterTexture != _environmentLighting.boundPrefilterTexture) {
        updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                               cubemapTexture,
                                               irradianceTexture,
                                               prefilterTexture,
                                               brdfLutTexture);
        _environmentLighting.boundCubemapTexture    = cubemapTexture;
        _environmentLighting.boundIrradianceTexture = irradianceTexture;
        _environmentLighting.boundPrefilterTexture  = prefilterTexture;
    }

    return _environmentLighting.sceneDS;
}

RenderSharedResourceProvider::EnvironmentLightingTextureSet RenderSharedResourceProvider::resolveSceneEnvironmentLightingTextures(Scene* scene) const
{
    EnvironmentLightingTextureSet resources{};

    if (!scene && _app && _app->getSceneManager()) {
        scene = _app->getSceneManager()->getActiveScene();
    }

    resources.cubemapTexture    = findSceneEnvironmentCubemapTexture(scene);
    resources.irradianceTexture = findSceneEnvironmentIrradianceTexture(scene);
    resources.prefilterTexture  = findSceneEnvironmentPrefilterTexture(scene);
    resources.brdfLutTexture    = _sharedResources.pbrLUT.get();
    if (!resources.cubemapTexture) {
        resources.cubemapTexture = _skybox.fallbackTexture.get();
    }
    if (!resources.irradianceTexture) {
        resources.irradianceTexture = _environmentLighting.fallbackIrradianceTexture.get();
    }
    if (!resources.prefilterTexture) {
        resources.prefilterTexture = _environmentLighting.fallbackPrefilterTexture.get();
    }

    return resources;
}

void RenderSharedResourceProvider::init(IRender* render, App* app)
{
    _render = render;
    _app    = app;

    initSharedPipelineResources();
    initSkyboxResources();
    initEnvironmentLightingResources();
}

void RenderSharedResourceProvider::shutdown()
{
    releaseRenderOwnedResources();
    _app    = nullptr;
    _render = nullptr;
}

void RenderSharedResourceProvider::initSharedPipelineResources()
{
    _cubemapSampler = _render->getResourceFactory()->createSampler(SamplerDesc{
        .label        = "App_SkyboxSampler",
        .addressModeU = ESamplerAddressMode::Repeat,
        .addressModeV = ESamplerAddressMode::Repeat,
        .addressModeW = ESamplerAddressMode::Repeat,
    });

    _pbrGenerateBrdfLUT.init(_render);
    _sharedResources.pbrLUT = createRenderImage(
        *_render->getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = "App_PBR_BRDF_LUT",
                .format        = EFormat::R16G16B16A16_SFLOAT,
                .extent        = {.width = 512, .height = 512, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = "App_PBR_BRDF_LUT_DefaultView",
                .aspectFlags = EImageAspect::Color,
            },
        });
    YA_CORE_ASSERT(_sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView(),
                   "Failed to create PBR BRDF LUT render texture");
    if (_sharedResources.pbrLUT) {
        auto*      cmdBuf = _render->beginIsolateCommands("App_PBR_BRDF_LUT");
        const auto result = _pbrGenerateBrdfLUT.execute({
            .cmdBuf = cmdBuf,
            .output = _sharedResources.pbrLUT.get(),
        });
        _render->endIsolateCommands(cmdBuf);
        YA_CORE_ASSERT(result.bSuccess, "Failed to generate PBR BRDF LUT");
    }
}

void RenderSharedResourceProvider::initSkyboxResources()
{
    _skybox.dsl = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "App_Skybox_CubeMap_DSL",
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Fragment,
                },
            },
        });

    _skybox.dsp = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "App_Skybox_DSP",
            .maxSets   = 4,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 4,
                },
            },
        });

    _skybox.fallbackTexture = Texture::createSolidCubeMap(ColorU8_t{0, 0, 0, 255}, "App_FallbackSkybox");
    YA_CORE_ASSERT(_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView(),
                   "Failed to create fallback skybox cubemap");

    _skybox.fallbackDS = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    _skybox.sceneDS    = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    updateSkyboxDescriptorSet(_skybox.fallbackDS, _skybox.fallbackTexture.get());
    updateSkyboxDescriptorSet(_skybox.sceneDS, _skybox.fallbackTexture.get());
}

void RenderSharedResourceProvider::initEnvironmentLightingResources()
{
    _environmentLighting.dsl = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "App_EnvironmentLighting_DSL",
            .bindings = {
                DescriptorSetLayoutBinding{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                DescriptorSetLayoutBinding{.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                DescriptorSetLayoutBinding{.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                DescriptorSetLayoutBinding{.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        });

    _environmentLighting.dsp = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "App_EnvironmentLighting_DSP",
            .maxSets   = 2,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 8,
                },
            },
        });

    _environmentLighting.fallbackIrradianceTexture = Texture::createSolidCubeMap(ColorU8_t{0, 0, 0, 255}, "App_FallbackIrradiance");
    YA_CORE_ASSERT(_environmentLighting.fallbackIrradianceTexture && _environmentLighting.fallbackIrradianceTexture->getImageView(),
                   "Failed to create fallback irradiance cubemap");
    _environmentLighting.fallbackPrefilterTexture = Texture::createSolidCubeMap(ColorU8_t{0, 0, 0, 255}, "App_FallbackPrefilter");
    YA_CORE_ASSERT(_environmentLighting.fallbackPrefilterTexture && _environmentLighting.fallbackPrefilterTexture->getImageView(),
                   "Failed to create fallback prefilter cubemap");

    _environmentLighting.fallbackDS = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    _environmentLighting.sceneDS    = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    updateEnvironmentLightingDescriptorSet(_environmentLighting.fallbackDS,
                                           _skybox.fallbackTexture.get(),
                                           _environmentLighting.fallbackIrradianceTexture.get(),
                                           _environmentLighting.fallbackPrefilterTexture.get(),
                                           _sharedResources.pbrLUT.get());
    updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                           _skybox.fallbackTexture.get(),
                                           _environmentLighting.fallbackIrradianceTexture.get(),
                                           _environmentLighting.fallbackPrefilterTexture.get(),
                                           _sharedResources.pbrLUT.get());
}

void RenderSharedResourceProvider::releaseRenderOwnedResources()
{
    _skybox.fallbackTexture.reset();
    _skybox.boundSceneTexture = nullptr;
    _skybox.sceneDS           = nullptr;
    _skybox.fallbackDS        = nullptr;
    _skybox.dsp.reset();
    _skybox.dsl.reset();

    _environmentLighting.fallbackIrradianceTexture.reset();
    _environmentLighting.fallbackPrefilterTexture.reset();
    _environmentLighting.boundCubemapTexture    = nullptr;
    _environmentLighting.boundIrradianceTexture = nullptr;
    _environmentLighting.boundPrefilterTexture  = nullptr;
    _environmentLighting.sceneDS                = nullptr;
    _environmentLighting.fallbackDS             = nullptr;
    _environmentLighting.dsp.reset();
    _environmentLighting.dsl.reset();
    _sharedResources.pbrLUT.reset();
    _pbrGenerateBrdfLUT.shutdown();

    _cubemapSampler.reset();
}

void RenderSharedResourceProvider::resetSkyboxPool()
{
    if (!_skybox.dsp || !_skybox.dsl) {
        return;
    }

    _skybox.dsp->resetPool();
    _skybox.sceneDS           = nullptr;
    _skybox.fallbackDS        = nullptr;
    _skybox.boundSceneTexture = nullptr;

    _skybox.fallbackDS = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    _skybox.sceneDS    = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    YA_CORE_ASSERT(_skybox.fallbackDS, "Failed to re-allocate fallback skybox descriptor set");
    YA_CORE_ASSERT(_skybox.sceneDS, "Failed to re-allocate scene skybox descriptor set");

    if (_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView()) {
        updateSkyboxDescriptorSet(_skybox.fallbackDS, _skybox.fallbackTexture.get());
        updateSkyboxDescriptorSet(_skybox.sceneDS, _skybox.fallbackTexture.get());
    }
}

void RenderSharedResourceProvider::resetEnvironmentLightingPool()
{
    if (!_environmentLighting.dsp || !_environmentLighting.dsl) {
        return;
    }

    _environmentLighting.dsp->resetPool();
    _environmentLighting.sceneDS                = nullptr;
    _environmentLighting.fallbackDS             = nullptr;
    _environmentLighting.boundCubemapTexture    = nullptr;
    _environmentLighting.boundIrradianceTexture = nullptr;
    _environmentLighting.boundPrefilterTexture  = nullptr;

    _environmentLighting.fallbackDS = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    _environmentLighting.sceneDS    = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    YA_CORE_ASSERT(_environmentLighting.fallbackDS, "Failed to re-allocate fallback environment lighting descriptor set");
    YA_CORE_ASSERT(_environmentLighting.sceneDS, "Failed to re-allocate scene environment lighting descriptor set");

    if (_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView() &&
        _environmentLighting.fallbackIrradianceTexture && _environmentLighting.fallbackIrradianceTexture->getImageView() &&
        _environmentLighting.fallbackPrefilterTexture && _environmentLighting.fallbackPrefilterTexture->getImageView() &&
        _sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView()) {
        updateEnvironmentLightingDescriptorSet(_environmentLighting.fallbackDS,
                                               _skybox.fallbackTexture.get(),
                                               _environmentLighting.fallbackIrradianceTexture.get(),
                                               _environmentLighting.fallbackPrefilterTexture.get(),
                                               _sharedResources.pbrLUT.get());
        updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                               _skybox.fallbackTexture.get(),
                                               _environmentLighting.fallbackIrradianceTexture.get(),
                                               _environmentLighting.fallbackPrefilterTexture.get(),
                                               _sharedResources.pbrLUT.get());
    }
}

} // namespace ya
