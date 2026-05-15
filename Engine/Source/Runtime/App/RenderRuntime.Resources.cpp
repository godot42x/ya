#include "RenderRuntime.h"

#include "App.h"
#include "DebugRenderSystem.h"
#include "Core/Async/TaskQueue.h"
#include "Core/Debug/RenderDocCapture.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Swapchain.h"
#include "Resource/AssetManager.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/Font/FontManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/Texture/TextureLibrary.h"

#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>

namespace ya
{

namespace
{

struct AutomationRenderDocSummaryResult
{
    bool        bSuccess = false;
    std::string capturePath;
    std::string passSummaryPath;
    std::string error;
};

std::string sanitizeCapturePath(std::string path)
{
    if (const size_t nullPos = path.find('\0'); nullPos != std::string::npos) {
        path.resize(nullPos);
    }
    return std::filesystem::path(path).generic_string();
}

std::string toProjectRelativePath(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absPath = std::filesystem::absolute(path, ec);
    if (ec) {
        return path.generic_string();
    }

    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return absPath.generic_string();
    }

    const auto rel = std::filesystem::relative(absPath, cwd, ec);
    if (ec || rel.empty()) {
        return absPath.generic_string();
    }

    return rel.generic_string();
}

AutomationRenderDocSummaryResult buildAutomationRenderDocPassSummary(const std::string& capturePath)
{
    AutomationRenderDocSummaryResult result;
    const std::string normalizedCapturePath = sanitizeCapturePath(capturePath);
    if (normalizedCapturePath.empty()) {
        result.error = "capture path is empty";
        return result;
    }

    const auto capture = std::filesystem::absolute(std::filesystem::path(normalizedCapturePath));
    result.capturePath = toProjectRelativePath(capture);
    if (!std::filesystem::exists(capture)) {
        result.error = std::format("capture does not exist: {}", capture.generic_string());
        return result;
    }

    const auto scriptPath = std::filesystem::absolute(std::filesystem::path("Script/renderdoc/rdc_pass_summary.py"));
    if (!std::filesystem::exists(scriptPath)) {
        result.error = std::format("summary script not found: {}", scriptPath.generic_string());
        return result;
    }

    const auto summaryPath = capture.parent_path() / "pass_summary.json";
#if defined(_WIN32)
    const std::string command = std::format(R"(py -3 "{}" --input "{}" --output "{}")",
                                            scriptPath.generic_string(),
                                            capture.generic_string(),
                                            summaryPath.generic_string());
#else
    const std::string command = std::format(R"(python3 "{}" --input "{}" --output "{}")",
                                            scriptPath.generic_string(),
                                            capture.generic_string(),
                                            summaryPath.generic_string());
#endif
    const int exitCode = std::system(command.c_str());
    if (exitCode != 0) {
        result.error = std::format("summary script exited with code {}", exitCode);
        return result;
    }
    if (!std::filesystem::exists(summaryPath)) {
        result.error = std::format("summary output not found: {}", summaryPath.generic_string());
        return result;
    }

    result.bSuccess        = true;
    result.passSummaryPath = toProjectRelativePath(summaryPath);
    return result;
}

void openCaptureDirectoryInOS(const std::string& filePath)
{
    if (filePath.empty()) {
        YA_CORE_WARN("File path is empty, cannot open directory");
        return;
    }

    auto dir = std::filesystem::path(filePath).parent_path();
    if (dir.empty()) {
        YA_CORE_WARN("Directory path is empty for file: {}", filePath);
        return;
    }

    dir = std::filesystem::absolute(dir);
    const auto url = std::format("file:///{}", dir.string());
    if (!SDL_OpenURL(url.c_str())) {
        YA_CORE_ERROR("Failed to open directory {}: {}", dir.string(), SDL_GetError());
    }
}

} // namespace

Texture* RenderRuntime::findSceneSkyboxTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneSkyboxTexture(scene);
}

Texture* RenderRuntime::findSceneEnvironmentCubemapTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentCubemapTexture(scene);
}

Texture* RenderRuntime::findSceneEnvironmentIrradianceTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentIrradianceTexture(scene);
}

Texture* RenderRuntime::findSceneEnvironmentPrefilterTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneEnvironmentPrefilterTexture(scene);
}

void RenderRuntime::updateSkyboxDescriptorSet(DescriptorSetHandle ds, Texture* texture)
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

void RenderRuntime::updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds,
                                                           Texture*            cubemapTexture,
                                                           Texture*            irradianceTexture,
                                                           Texture*            prefilterTexture,
                                                           Texture*            brdfLutTexture)
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

DescriptorSetHandle RenderRuntime::getSceneSkyboxDescriptorSet(Scene* scene)
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

DescriptorSetHandle RenderRuntime::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    if (!_environmentLighting.sceneDS) {
        return _environmentLighting.fallbackDS;
    }

    if (!scene && _app && _app->getSceneManager()) {
        scene = _app->getSceneManager()->getActiveScene();
    }

    auto* cubemapTexture    = findSceneEnvironmentCubemapTexture(scene);
    auto* irradianceTexture = findSceneEnvironmentIrradianceTexture(scene);
    auto* prefilterTexture  = findSceneEnvironmentPrefilterTexture(scene);
    auto* brdfLutTexture    = _sharedResources.pbrLUT.get();
    if (!cubemapTexture) {
        cubemapTexture = _skybox.fallbackTexture.get();
    }
    if (!irradianceTexture) {
        irradianceTexture = _environmentLighting.fallbackIrradianceTexture.get();
    }
    if (!prefilterTexture) {
        prefilterTexture = _environmentLighting.fallbackPrefilterTexture.get();
    }

    if (!cubemapTexture || !irradianceTexture || !prefilterTexture || !brdfLutTexture) {
        return _environmentLighting.fallbackDS;
    }

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

void RenderRuntime::init(const InitDesc& desc)
{
    YA_PROFILE_FUNCTION_LOG();
    YA_CORE_ASSERT(desc.app && desc.appDesc, "RenderRuntime init requires App and AppDesc");

    const AppDesc& appDesc = *desc.appDesc;

    initRuntimeState(desc);
    initShaderSystems();
    initDiagnostics(appDesc);
    initRenderBackend(appDesc);
    initResourceCaches();
    initSharedRenderResources();
    initPresentationResources();
    initCommandResources();
    initFrameServices();
}

void RenderRuntime::initRuntimeState(const InitDesc& desc)
{
    _app = desc.app;

    const AppDesc& appDesc = *desc.appDesc;
    currentRenderAPI       = ERenderAPI::Vulkan;
    _viewportRect          = Rect2D{
                 .pos    = {0.0f, 0.0f},
                 .extent = {static_cast<float>(appDesc.width), static_cast<float>(appDesc.height)},
    };
}

void RenderRuntime::initShaderSystems()
{
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();
    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);

    auto slangProcessor = ShaderProcessorFactory()
                              .withProcessorType(ShaderProcessorFactory::EProcessorType::Slang)
                              .withShaderStoragePath("Engine/Shader/Slang")
                              .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
                              .FactoryNew<SlangProcessor>();
    _shaderStorage->setSlangProcessor(slangProcessor);

    _shaderStorage->preloadAsync({
        ShaderDesc{.shaderName = "Test/Unlit.glsl"},
        ShaderDesc{.shaderName = "Test/SimpleMaterial.glsl"},
        ShaderDesc{.shaderName = "Sprite2D_Screen.glsl"},
        ShaderDesc{.shaderName = "Sprite2D_World.glsl"},
        ShaderDesc{.shaderName = "Test/DebugRender.glsl"},
        ShaderDesc{.shaderName = "Test/DebugPrimitiveLine.glsl"},
        ShaderDesc{.shaderName = "Test/DebugPrimitiveShape.glsl"},
        ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"},
        ShaderDesc{.shaderName = "Skybox.glsl"},
        ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"},
        ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowCull.comp.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowIndirect.slang"},
        ShaderDesc{.shaderName = "Misc/pbr_generate_brdf_lut.slang"},
    });
    _deleter.push("ShaderStorage", [this](void*)
                  { _shaderStorage.reset(); });
}

void RenderRuntime::initDiagnostics(const AppDesc& appDesc)
{
    if (!appDesc.bEnableRenderDoc) {
        return;
    }

    _renderDoc.capture             = ya::makeShared<RenderDocCapture>();
    _renderDoc.configuredDllPath   = appDesc.renderDocDllPath;
    _renderDoc.configuredOutputDir = appDesc.renderDocCaptureOutputDir;
    if (!_renderDoc.capture->init(_renderDoc.configuredDllPath, _renderDoc.configuredOutputDir)) {
        YA_CORE_WARN("RenderDoc unavailable: failed to initialize with dll '{}'", _renderDoc.configuredDllPath);
    }
    _renderDoc.capture->setCaptureFinishedCallback([this](const RenderDocCapture::CaptureResult& result)
                                                  {
        const bool bAutomationCapture = _renderDoc.bAutomationCaptureRequested;
        if (bAutomationCapture) {
            _renderDoc.bAutomationCaptureRequested = false;
            _renderDoc.lastCapturePath             = sanitizeCapturePath(result.capturePath);
        }

        if (!result.bSuccess) {
            if (bAutomationCapture) {
                _renderDoc.bAutomationCaptureFinished     = false;
                _renderDoc.bAutomationCaptureFailed       = true;
                _renderDoc.bAutomationPostProcessPending  = false;
                _renderDoc.automationPassSummaryPath.clear();
                YA_CORE_WARN("Automation RenderDoc capture failed");
            }
            return;
        }

        _renderDoc.lastCapturePath = sanitizeCapturePath(result.capturePath);
        if (bAutomationCapture) {
            YA_CORE_INFO("Automation RenderDoc capture finished: {}", _renderDoc.lastCapturePath);
            _renderDoc.onCaptureAction               = 0;
            _renderDoc.bAutomationCaptureFinished    = false;
            _renderDoc.bAutomationCaptureFailed      = false;
            _renderDoc.bAutomationPostProcessPending = true;
            _renderDoc.automationPassSummaryPath.clear();

            TaskQueue::get().submitWithCallback(
                [capturePath = _renderDoc.lastCapturePath]() {
                    return buildAutomationRenderDocPassSummary(capturePath);
                },
                [this](AutomationRenderDocSummaryResult summaryResult)
                {
                    _renderDoc.bAutomationPostProcessPending = false;
                    _renderDoc.lastCapturePath               = std::move(summaryResult.capturePath);
                    if (!summaryResult.bSuccess) {
                        _renderDoc.bAutomationCaptureFinished = false;
                        _renderDoc.bAutomationCaptureFailed   = true;
                        _renderDoc.automationPassSummaryPath.clear();
                        YA_CORE_WARN("Automation RenderDoc pass summary failed for {}: {}",
                                     _renderDoc.lastCapturePath,
                                     summaryResult.error);
                        return;
                    }

                    _renderDoc.automationPassSummaryPath  = std::move(summaryResult.passSummaryPath);
                    _renderDoc.bAutomationCaptureFinished = true;
                    _renderDoc.bAutomationCaptureFailed   = false;
                    YA_CORE_INFO("Automation RenderDoc pass summary generated: {}",
                                 _renderDoc.automationPassSummaryPath);
                });
            return;
        }

        switch (_renderDoc.onCaptureAction) {
        case 0:
        case 1:
            if (!_renderDoc.capture->launchReplayUI(true, nullptr)) {
                YA_CORE_WARN("RenderDoc: failed to launch replay UI");
            }
            break;
        case 2:
            openCaptureDirectoryInOS(result.capturePath);
            break;
        default:
            break;
        } });
    _deleter.push("RenderDocCapture", [this](void*)
                  {
        if (_renderDoc.capture) {
            _renderDoc.capture->shutdown();
            _renderDoc.capture.reset();
        } });
}

void RenderRuntime::initRenderBackend(const AppDesc& appDesc)
{
    const bool bNeedPresentationTransferSrc = appDesc.automation.screenshotPath.has_value() &&
                                              appDesc.automation.screenshotTarget == EAutomationScreenshotTarget::Editor;

    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat        = App::LINEAR_FORMAT,
            .bVsync             = false,
            .minImageCount      = 3,
            .bEnableTransferSrc = bNeedPresentationTransferSrc,
            .width              = static_cast<uint32_t>(appDesc.width),
            .height             = static_cast<uint32_t>(appDesc.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

    if (!_renderDoc.capture) {
        return;
    }

    _renderDoc.capture->setRenderContext({
        .device    = _render->as<VulkanRender>()->getDevice(),
        .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
    });
    _render->getSwapchain()->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
        {
            (void)old;
            (void)now;
            (void)bImageRecreated;
            _renderDoc.capture->setRenderContext({
                .device    = _render->as<VulkanRender>()->getDevice(),
                .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
            });
        });
}

void RenderRuntime::initResourceCaches()
{
    TextureLibrary::get().init();

    ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100);
    ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);
    ResourceRegistry::get().registerCache(FontManager::get(), 80);
    ResourceRegistry::get().registerCache(AssetManager::get(), 70);
}

void RenderRuntime::initSharedRenderResources()
{
    initSharedPipelineResources();
    initSkyboxResources();
    initEnvironmentLightingResources();

    _deleter.push("RenderBindings", [this](void*)
                  { releaseRenderOwnedResources(); });

    _shaderStorage->waitForPreload();
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});

    initActivePipeline();
}

void RenderRuntime::initSharedPipelineResources()
{
    _cubemapSampler = Sampler::create(SamplerDesc{
        .label        = "App_SkyboxSampler",
        .addressModeU = ESamplerAddressMode::Repeat,
        .addressModeV = ESamplerAddressMode::Repeat,
        .addressModeW = ESamplerAddressMode::Repeat,
    });

    _pbrGenerateBrdfLUT.init(_render);
    _sharedResources.pbrLUT = Texture::createRenderTexture(RenderTextureCreateInfo{
        .label     = "App_PBR_BRDF_LUT",
        .width     = 512,
        .height    = 512,
        .format    = EFormat::R16G16B16A16_SFLOAT,
        .usage     = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .mipLevels = 1,
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

void RenderRuntime::initSkyboxResources()
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

void RenderRuntime::initEnvironmentLightingResources()
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

void RenderRuntime::initPresentationResources()
{
    _screenRenderPass = nullptr;
    _screenRT         = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Final RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = true,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = _render->getSwapchain()->getFormat(),
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::Undefined,
                    .finalLayout    = EImageLayout::PresentSrcKHR,
                    .usage          = EImageUsage::ColorAttachment,
                },
            },
        },
    });

    _render->getSwapchain()->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
        {
            const bool bExtentChanged = (now.extent.width != old.extent.width ||
                                         now.extent.height != old.extent.height);
            const bool bPresentModeChanged = (old.presentMode != now.presentMode);

            if (bExtentChanged) {
                _screenRT->setExtent(Extent2D{
                    .width  = now.extent.width,
                    .height = now.extent.height,
                });
            }

            if (bImageRecreated || bPresentModeChanged) {
                _screenRT->recreate();
            }
        });

    _deleter.push("ScreenRT", [this](void*)
                  {
        if (_screenRT) {
            _screenRT->destroy();
            _screenRT.reset();
        }
        _screenRenderPass.reset(); });
}

void RenderRuntime::initCommandResources()
{
    std::vector<stdptr<ICommandBuffer>> cmdBufs;
    _render->allocateCommandBuffers(_render->getSwapchainImageCount() + 1, cmdBufs);
    _commandBuffers.assign(cmdBufs.begin(), cmdBufs.begin() + _render->getSwapchainImageCount());
    _offscreenCmdBuf = cmdBufs.back();
    _deleter.push("CmdBufs", [this](void*)
                  {
        _commandBuffers.clear();
        _offscreenCmdBuf.reset(); });

    auto*             vkRender = static_cast<VulkanRender*>(_render);
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    VkFence  fence = VK_NULL_HANDLE;
    VkResult ret   = vkCreateFence(vkRender->getDevice(), &fenceCI, nullptr, &fence);
    YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create offscreen fence");
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_FENCE, fence, "OffscreenFence");
    _offscreenFence   = fence;
    _offscreenPending = false;
    _deleter.push("OffscreenFence", [this](void*)
                  {
        if (_offscreenFence) {
            auto* vkR = static_cast<VulkanRender*>(_render);
            vkDestroyFence(vkR->getDevice(), static_cast<VkFence>(_offscreenFence), nullptr);
            _offscreenFence   = nullptr;
            _offscreenPending = false;
        } });
}

void RenderRuntime::initFrameServices()
{
    ImGuiManager::get().init(_render, nullptr);
    _render->waitIdle();

    DeferredDeletionQueue::get().init(/*framesInFlight=*/1);
    TaskQueue::get().start(/*numThreads=*/2);
}

void RenderRuntime::shutdown()
{
    shutdownRuntimeServices();
    shutdownActivePipeline();
    getDebugRenderSystem().destroy();
    _deleter.clear();
    destroyRenderBackend();
}

void RenderRuntime::shutdownRuntimeServices()
{
    if (_render) {
        _render->waitIdle();
    }

    ImGuiManager::get().shutdown();
    TaskQueue::get().stop();
    ResourceRegistry::get().clearAll();
}

void RenderRuntime::destroyRenderBackend()
{
    if (!_render) {
        return;
    }

    DeferredDeletionQueue::get().flushAll();

    _render->destroy();
    delete _render;
    _render = nullptr;
}

void RenderRuntime::releaseRenderOwnedResources()
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

void RenderRuntime::resetSkyboxPool()
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

void RenderRuntime::resetEnvironmentLightingPool()
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
