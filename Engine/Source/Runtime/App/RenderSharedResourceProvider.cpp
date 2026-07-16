#include "RenderSharedResourceProvider.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/RenderImage.h"

#include "App.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Render/Core/Sampler.h"

namespace ya
{

namespace
{

IImageView* getTextureImageView(const Texture* texture)
{
    return texture ? texture->getImageView() : nullptr;
}

ImageViewHandle getTextureImageViewHandle(const Texture* texture)
{
    return texture && texture->getImageView() ? texture->getImageView()->getHandle() : ImageViewHandle{};
}

ImageViewHandle getRenderImageViewHandle(const RenderImage* image)
{
    return image && image->getImageView() ? image->getImageView()->getHandle() : ImageViewHandle{};
}

const char* getTextureLabel(const Texture* texture)
{
    return texture ? texture->getLabel().c_str() : "<null>";
}

const char* getRenderImageLabel(const RenderImage* image)
{
    return image ? image->getLabel().c_str() : "<null>";
}

IImageView* resolveDescriptorImageView(const stdptr<Texture>& texture, const stdptr<RenderImage>& renderImage)
{
    if (renderImage && renderImage->getImageView()) {
        return renderImage->getImageView();
    }
    return getTextureImageView(texture.get());
}

} // namespace

void RenderSharedResourceProvider::updateSkyboxDescriptorSet(DescriptorSetHandle ds, IImageView* imageView)
{
    if (!ds || !imageView || !_cubemapSampler) {
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
                        imageView->getHandle(),
                        _cubemapSampler->getHandle(),
                        EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});
}

void RenderSharedResourceProvider::updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds,
                                                                          IImageView*         cubemapImageView,
                                                                          IImageView*         irradianceImageView,
                                                                          IImageView*         prefilterImageView,
                                                                          IImageView*         brdfLutImageView)
{
    if (!ds || !cubemapImageView || !irradianceImageView || !prefilterImageView || !brdfLutImageView || !_cubemapSampler) {
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
                        cubemapImageView->getHandle(),
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
                        irradianceImageView->getHandle(),
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
                        prefilterImageView->getHandle(),
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
                        brdfLutImageView->getHandle(),
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

    auto* resolver = (_app ? _app->getResourceResolveSystem() : nullptr);
    auto  skyboxResource = resolver ? resolver->resolveSceneSkyboxResource(scene) : ImageResourceRef{};
    auto* descriptorImageView = skyboxResource.getImageView();
    if (!descriptorImageView) {
        _skybox.boundSceneImageView = nullptr;
        return _skybox.fallbackDS;
    }

    const auto imageViewHandle = descriptorImageView->getHandle();
    if (imageViewHandle != _skybox.boundSceneImageView) {
        updateSkyboxDescriptorSet(_skybox.sceneDS, descriptorImageView);
        _skybox.boundSceneImageView = imageViewHandle;
    }

    return _skybox.sceneDS;
}

DescriptorSetHandle RenderSharedResourceProvider::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    if (!_environmentLighting.sceneDS) {
        return _environmentLighting.fallbackDS;
    }

    auto resources = resolveSceneEnvironmentLightingResources(scene);
    if (!resources.isComplete()) {
        YA_CORE_WARN("Environment lighting DS fallback: incomplete resources cubemapImage='{}' irradianceImage='{}' prefilterImage='{}' brdf='{}'",
                     getRenderImageLabel(resources.cubemap.renderImage.get()),
                     getRenderImageLabel(resources.irradiance.renderImage.get()),
                     getRenderImageLabel(resources.prefilter.renderImage.get()),
                     getRenderImageLabel(resources.brdfLut.get()));
        return _environmentLighting.fallbackDS;
    }

    auto* cubemapImageView = resources.cubemap.getImageView();
    auto* irradianceImageView = resources.irradiance.getImageView();
    auto* prefilterImageView = resources.prefilter.getImageView();
    const auto& brdfLutTexture = resources.brdfLut;
    auto*       brdfLutImageView = brdfLutTexture ? brdfLutTexture->getImageView() : nullptr;
    if (!cubemapImageView || !irradianceImageView || !prefilterImageView || !brdfLutImageView) {
        YA_CORE_WARN("Environment lighting DS fallback after compat resolve: cubemap='{}' irradiance='{}' prefilter='{}' brdf='{}'",
                     getTextureLabel(resources.cubemap.texture.get()),
                     getTextureLabel(resources.irradiance.texture.get()),
                     getTextureLabel(resources.prefilter.texture.get()),
                     getRenderImageLabel(brdfLutTexture.get()));
        return _environmentLighting.fallbackDS;
    }

    const auto cubemapImageViewHandle    = cubemapImageView->getHandle();
    const auto irradianceImageViewHandle = irradianceImageView->getHandle();
    const auto prefilterImageViewHandle  = prefilterImageView->getHandle();
    const auto brdfLutImageViewHandle    = brdfLutImageView->getHandle();

    if (cubemapImageViewHandle != _environmentLighting.boundCubemapImageView ||
        irradianceImageViewHandle != _environmentLighting.boundIrradianceImageView ||
        prefilterImageViewHandle != _environmentLighting.boundPrefilterImageView ||
        brdfLutImageViewHandle != _environmentLighting.boundBrdfLutImageView) {
        updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                               cubemapImageView,
                                               irradianceImageView,
                                               prefilterImageView,
                                               brdfLutImageView);
        _environmentLighting.boundCubemapImageView  = cubemapImageViewHandle;
        _environmentLighting.boundIrradianceImageView = irradianceImageViewHandle;
        _environmentLighting.boundPrefilterImageView  = prefilterImageViewHandle;
        _environmentLighting.boundBrdfLutImageView    = brdfLutImageViewHandle;
        YA_CORE_INFO("Environment lighting DS update: cubemap='{}' irradiance='{}' prefilter='{}' brdf='{}'",
                     getTextureLabel(resources.cubemap.texture.get()),
                     getTextureLabel(resources.irradiance.texture.get()),
                     getTextureLabel(resources.prefilter.texture.get()),
                     getRenderImageLabel(brdfLutTexture.get()));
    }

    return _environmentLighting.sceneDS;
}

EnvironmentLightingSceneResources RenderSharedResourceProvider::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    EnvironmentLightingSceneResources resources{};

    if (!scene && _app && _app->getSceneManager()) {
        scene = _app->getSceneManager()->getActiveScene();
    }

    if (_app && _app->getResourceResolveSystem()) {
        auto* resolver = _app->getResourceResolveSystem();
        resources = resolver->resolveSceneEnvironmentLightingResources(scene);
    }
    resources.brdfLut = _sharedResources.pbrLUT;
    if (!resources.cubemap.isValid()) {
        resources.cubemap.texture = _skybox.fallbackTexture;
    }
    if (!resources.irradiance.isValid()) {
        resources.irradiance.texture = _environmentLighting.fallbackIrradianceTexture;
    }
    if (!resources.prefilter.isValid()) {
        resources.prefilter.texture = _environmentLighting.fallbackPrefilterTexture;
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
    updateSkyboxDescriptorSet(_skybox.fallbackDS, _skybox.fallbackTexture->getImageView());
    updateSkyboxDescriptorSet(_skybox.sceneDS, _skybox.fallbackTexture->getImageView());
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
                                           _skybox.fallbackTexture->getImageView(),
                                           _environmentLighting.fallbackIrradianceTexture->getImageView(),
                                           _environmentLighting.fallbackPrefilterTexture->getImageView(),
                                           _sharedResources.pbrLUT->getImageView());
    updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                           _skybox.fallbackTexture->getImageView(),
                                           _environmentLighting.fallbackIrradianceTexture->getImageView(),
                                           _environmentLighting.fallbackPrefilterTexture->getImageView(),
                                           _sharedResources.pbrLUT->getImageView());
}

void RenderSharedResourceProvider::releaseRenderOwnedResources()
{
    _skybox.fallbackTexture.reset();
    _skybox.boundSceneImageView = nullptr;
    _skybox.sceneDS           = nullptr;
    _skybox.fallbackDS        = nullptr;
    _skybox.dsp.reset();
    _skybox.dsl.reset();

    _environmentLighting.fallbackIrradianceTexture.reset();
    _environmentLighting.fallbackPrefilterTexture.reset();
    _environmentLighting.boundCubemapImageView  = nullptr;
    _environmentLighting.boundIrradianceImageView = nullptr;
    _environmentLighting.boundPrefilterImageView  = nullptr;
    _environmentLighting.boundBrdfLutImageView    = nullptr;
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
    _skybox.boundSceneImageView = nullptr;

    _skybox.fallbackDS = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    _skybox.sceneDS    = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
    YA_CORE_ASSERT(_skybox.fallbackDS, "Failed to re-allocate fallback skybox descriptor set");
    YA_CORE_ASSERT(_skybox.sceneDS, "Failed to re-allocate scene skybox descriptor set");

    if (_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView()) {
        updateSkyboxDescriptorSet(_skybox.fallbackDS, _skybox.fallbackTexture->getImageView());
        updateSkyboxDescriptorSet(_skybox.sceneDS, _skybox.fallbackTexture->getImageView());
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
    _environmentLighting.boundCubemapImageView  = nullptr;
    _environmentLighting.boundIrradianceImageView = nullptr;
    _environmentLighting.boundPrefilterImageView  = nullptr;
    _environmentLighting.boundBrdfLutImageView    = nullptr;

    _environmentLighting.fallbackDS = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    _environmentLighting.sceneDS    = _environmentLighting.dsp->allocateDescriptorSets(_environmentLighting.dsl);
    YA_CORE_ASSERT(_environmentLighting.fallbackDS, "Failed to re-allocate fallback environment lighting descriptor set");
    YA_CORE_ASSERT(_environmentLighting.sceneDS, "Failed to re-allocate scene environment lighting descriptor set");

    if (_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView() &&
        _environmentLighting.fallbackIrradianceTexture && _environmentLighting.fallbackIrradianceTexture->getImageView() &&
        _environmentLighting.fallbackPrefilterTexture && _environmentLighting.fallbackPrefilterTexture->getImageView() &&
        _sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView()) {
        updateEnvironmentLightingDescriptorSet(_environmentLighting.fallbackDS,
                                               _skybox.fallbackTexture->getImageView(),
                                               _environmentLighting.fallbackIrradianceTexture->getImageView(),
                                               _environmentLighting.fallbackPrefilterTexture->getImageView(),
                                               _sharedResources.pbrLUT->getImageView());
        updateEnvironmentLightingDescriptorSet(_environmentLighting.sceneDS,
                                               _skybox.fallbackTexture->getImageView(),
                                               _environmentLighting.fallbackIrradianceTexture->getImageView(),
                                               _environmentLighting.fallbackPrefilterTexture->getImageView(),
                                               _sharedResources.pbrLUT->getImageView());
    }
}

} // namespace ya
