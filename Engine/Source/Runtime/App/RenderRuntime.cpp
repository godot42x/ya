#include "RenderRuntime.h"

#include "App.h"
#include "Core/Debug/RenderDocCapture.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Editor/EditorLayer.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include "ImGuiHelper.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Swapchain.h"

#include "Resource/AssetManager.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/FontManager.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/TextureLibrary.h"

#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include "Core/Async/TaskQueue.h"

#include "Core/UI/UIManager.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/System/ResourceResolveSystem.h"

#include "utility.cc/ranges.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

Texture* RenderRuntime::findSceneSkyboxTexture(Scene* scene) const
{
    if (!scene || !_app || !_app->getResourceResolveSystem()) {
        return nullptr;
    }

    return _app->getResourceResolveSystem()->findSceneSkyboxTexture(scene);
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

static void openDirectoryInOS(const std::string& filePath)
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
    dir     = std::filesystem::absolute(dir);
    auto p  = std::format("file:///{}", dir.string());
    bool ok = SDL_OpenURL(p.c_str());
    if (!ok) {
        YA_CORE_ERROR("Failed to open directory {}: {}", dir.string(), SDL_GetError());
    }
}

void RenderRuntime::onViewportResized(Rect2D rect)
{
    _viewportRect = rect;

    if (_forwardPipeline) {
        _forwardPipeline->onViewportResized(rect);
    }
    if (_deferredPipeline) {
        _deferredPipeline->onViewportResized(rect);
    }
}


void RenderRuntime::init(const InitDesc& desc)
{
    YA_PROFILE_FUNCTION_LOG();
    YA_CORE_ASSERT(desc.app && desc.appDesc, "RenderRuntime init requires App and AppDesc");

    _app              = desc.app;
    const AppDesc& ci = *desc.appDesc;

    currentRenderAPI = ERenderAPI::Vulkan;

    _viewportRect = Rect2D{
        .pos    = {0.0f, 0.0f},
        .extent = {static_cast<float>(ci.width), static_cast<float>(ci.height)},
    };

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

    // Launch async shader preloading — compiles on a background thread while
    // Vulkan context, descriptor pools, etc. are initialized on the main thread.
    _shaderStorage->preloadAsync({
        ShaderDesc{.shaderName = "Test/Unlit.glsl"},
        ShaderDesc{.shaderName = "Test/SimpleMaterial.glsl"},
        ShaderDesc{.shaderName = "Sprite2D_Screen.glsl"},
        ShaderDesc{.shaderName = "Sprite2D_World.glsl"},
        ShaderDesc{.shaderName = "Test/DebugRender.glsl"},
        ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"},
        ShaderDesc{.shaderName = "Skybox.glsl"},
        ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"},
        ShaderDesc{.shaderName = "Shadow/CombinedShadowMappingGenerate.glsl"},
        ShaderDesc{.shaderName = "Misc/pbr_generate_brdf_lut.slang"},
    });
    _deleter.push("ShaderStorage", [this](void*) {
        _shaderStorage.reset();
    });

    if (ci.bEnableRenderDoc) {
        _renderDocCapture             = ya::makeShared<RenderDocCapture>();
        _renderDocConfiguredDllPath   = ci.renderDocDllPath;
        _renderDocConfiguredOutputDir = ci.renderDocCaptureOutputDir;
        _renderDocCapture->init(_renderDocConfiguredDllPath, _renderDocConfiguredOutputDir);
        _renderDocCapture->setCaptureFinishedCallback([this](const RenderDocCapture::CaptureResult& result) {
            if (!result.bSuccess) {
                return;
            }
            _renderDocLastCapturePath = result.capturePath;
            switch (_renderDocOnCaptureAction) {
            case 0:
            case 1:
                if (!_renderDocCapture->launchReplayUI(true, nullptr)) {
                    YA_CORE_WARN("RenderDoc: failed to launch replay UI");
                }
                break;
            case 2:
                openDirectoryInOS(result.capturePath);
                break;
            default:
                break;
            }
        });
        _deleter.push("RenderDocCapture", [this](void*) {
            if (_renderDocCapture) {
                _renderDocCapture->shutdown();
                _renderDocCapture.reset();
            }
        });
    }

    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = App::LINEAR_FORMAT,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(ci.width),
            .height        = static_cast<uint32_t>(ci.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

    if (ci.bEnableRenderDoc) {
        _renderDocCapture->setRenderContext({
            .device    = _render->as<VulkanRender>()->getDevice(),
            .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
        });
        _render->getSwapchain()->onRecreate.addLambda(
            this,
            [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated) {
                (void)old;
                (void)now;
                (void)bImageRecreated;
                _renderDocCapture->setRenderContext({
                    .device    = _render->as<VulkanRender>()->getDevice(),
                    .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
                });
            });
    }

    {
        TextureLibrary::get().init();

        ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100);
        ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);
        ResourceRegistry::get().registerCache(FontManager::get(), 80);
        ResourceRegistry::get().registerCache(AssetManager::get(), 70);
    }

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
            auto* cmdBuf = _render->beginIsolateCommands("App_PBR_BRDF_LUT");
            const auto result = _pbrGenerateBrdfLUT.execute({
                .cmdBuf = cmdBuf,
                .output = _sharedResources.pbrLUT.get(),
            });
            _render->endIsolateCommands(cmdBuf);
            YA_CORE_ASSERT(result.bSuccess, "Failed to generate PBR BRDF LUT");
        }

        _skybox.fallbackTexture = Texture::createSolidCubeMap(ColorU8_t{0, 0, 0, 255}, "App_FallbackSkybox");
        YA_CORE_ASSERT(_skybox.fallbackTexture && _skybox.fallbackTexture->getImageView(),
                       "Failed to create fallback skybox cubemap");

        _skybox.fallbackDS = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
        _skybox.sceneDS    = _skybox.dsp->allocateDescriptorSets(_skybox.dsl);
        updateSkyboxDescriptorSet(_skybox.fallbackDS, _skybox.fallbackTexture.get());
        updateSkyboxDescriptorSet(_skybox.sceneDS, _skybox.fallbackTexture.get());

        _environmentLighting.dsl = IDescriptorSetLayout::create(
            _render,
            DescriptorSetLayoutDesc{
                .label    = "App_EnvironmentLighting_DSL",
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 3,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
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

        _deleter.push("RenderBindings", [this](void*) {
            releaseRenderOwnedResources();
        });
    }

    // Wait for all shaders to finish compiling before creating pipelines
    _shaderStorage->waitForPreload();
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});

    initActivePipeline();

    {
        _screenRenderPass = nullptr;

        _screenRT = ya::createRenderTarget(RenderTargetCreateInfo{
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
            [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated) {
                const bool bExtentChanged      = (now.extent.width != old.extent.width ||
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

        _deleter.push("ScreenRT", [this](void*) {
            if (_screenRT) {
                _screenRT->destroy();
                _screenRT.reset();
            }
            _screenRenderPass.reset();
        });
    }

    std::vector<stdptr<ICommandBuffer>> cmdBufs;
    _render->allocateCommandBuffers(_render->getSwapchainImageCount() + 1, cmdBufs);
    _commandBuffers.assign(cmdBufs.begin(), cmdBufs.begin() + _render->getSwapchainImageCount());
    _offscreenCmdBuf = cmdBufs.back();
    _deleter.push("CmdBufs", [this](void*) {
        _commandBuffers.clear();
        _offscreenCmdBuf.reset();
    });

    // Create a dedicated fence for offscreen work so we don't need waitIdle().
    {
        auto* vkRender = static_cast<VulkanRender*>(_render);
        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0, // initially unsignaled
        };
        VkFence fence = VK_NULL_HANDLE;
        VkResult ret = vkCreateFence(vkRender->getDevice(), &fenceCI, nullptr, &fence);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create offscreen fence");
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FENCE, fence, "OffscreenFence");
        _offscreenFence   = fence;
        _offscreenPending = false;
        _deleter.push("OffscreenFence", [this](void*) {
            if (_offscreenFence) {
                auto* vkR = static_cast<VulkanRender*>(_render);
                vkDestroyFence(vkR->getDevice(), static_cast<VkFence>(_offscreenFence), nullptr);
                _offscreenFence   = nullptr;
                _offscreenPending = false;
            }
        });
    }

    ImGuiManager::get().init(_render, nullptr);
    _render->waitIdle();

    // ── Resource management subsystems ──────────────────────────────────
    // Initialize GPU-safe deferred deletion queue.
    // framesInFlight: resources are kept alive for this many extra frames
    // after removal from the cache, guaranteeing the GPU has finished with them.
    DeferredDeletionQueue::get().init(/*framesInFlight=*/1);

    // Start async IO task queue (file reading, texture decoding).
    TaskQueue::get().start(/*numThreads=*/2);
}

void RenderRuntime::shutdown()
{
    if (_render) {
        _render->waitIdle();
    }

    shutdownActivePipeline();

    ImGuiManager::get().shutdown();

    ResourceRegistry::get().clearAll();

    TaskQueue::get().stop();

    _deleter.clear();

    if (_render) {
        // GPU is idle — safe to execute ALL pending resource destructors now.
        DeferredDeletionQueue::get().flushAll();

        _render->destroy();
        delete _render;
        _render = nullptr;
    }
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

    // Return cached skybox descriptor sets back to the pool and rebuild them.
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

void RenderRuntime::offScreenRender()
{
    if (!_render || !_app || !_offscreenCmdBuf || !_app->taskManager.hasOffscreenTasks()) {
        return;
    }

    // Wait for the previous offscreen batch to finish (fence, not waitIdle).
    if (_offscreenPending && _offscreenFence) {
        auto* vkRender = static_cast<VulkanRender*>(_render);
        VkFence fence = static_cast<VkFence>(_offscreenFence);
        vkWaitForFences(vkRender->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vkRender->getDevice(), 1, &fence);
        _offscreenPending = false;
    }

    auto cmdBuf = _offscreenCmdBuf;
    cmdBuf->reset();
    if (!cmdBuf->begin()) {
        YA_CORE_ERROR("Failed to begin offscreen command buffer");
        return;
    }

    _app->taskManager.updateOffscreenTasks(cmdBuf.get());

    if (!cmdBuf->end()) {
        YA_CORE_ERROR("Failed to end offscreen command buffer");
        return;
    }

    // Submit with fence — does not block.
    _render->submitToQueue({cmdBuf->getHandle()}, {}, {}, _offscreenFence);
    _offscreenPending = true;
}

// MARK: render
void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_FUNCTION()

    applyPendingShadingModelSwitch();

    offScreenRender();

    if (_viewportRect.extent.x <= 0 || _viewportRect.extent.y <= 0) {
        if (input.viewportRect.extent.x > 0 && input.viewportRect.extent.y > 0) {
            onViewportResized(input.viewportRect);
        }
        else {
            auto swapchainExtent = _render->getSwapchain()->getExtent();
            onViewportResized(Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height)},
            });
        }
    }
    _viewportFrameBufferScale = input.viewportFrameBufferScale;

    if (_renderDocCapture) {
        _renderDocCapture->onFrameBegin();
    }


    if (_render->getSwapchain()->getExtent().width <= 0 || _render->getSwapchain()->getExtent().height <= 0) {
        return;
    }

    _render->waitIdle();

    int32_t imageIndex = -1;
    if (!_render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    auto cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline->tick(ForwardRenderPipeline::TickDesc{
            .cmdBuf                   = cmdBuf.get(),
            .dt                       = input.dt,
            .view                     = input.view,
            .projection               = input.projection,
            .cameraPos                = input.cameraPos,
            .viewportRect             = _viewportRect,
            .viewportFrameBufferScale = _viewportFrameBufferScale,
            .appMode                  = static_cast<int>(input.appMode),
            .clicked                  = input.clicked,
            .frameData                = input.frameData,
        });
    }
    else {
        _deferredPipeline->tick(DeferredRenderPipeline::TickDesc{
            .cmdBuf                   = cmdBuf.get(),
            .sceneManager             = input.sceneManager,
            .dt                       = input.dt,
            .view                     = input.view,
            .projection               = input.projection,
            .cameraPos                = input.cameraPos,
            .viewportRect             = _viewportRect,
            .viewportFrameBufferScale = _viewportFrameBufferScale,
            .appMode                  = static_cast<int>(input.appMode),
            .clicked                  = input.clicked,
            .frameData                = input.frameData,
        });
    }

    const bool bViewportPassOpen = (_shadingModel == EShadingModel::Forward)
                                     ? (_forwardPipeline && _forwardPipeline->hasOpenViewportPass())
                                     : (_deferredPipeline && _deferredPipeline->hasOpenViewportPass());

    if (bViewportPassOpen) {
        YA_PROFILE_SCOPE("Render2D")

        const Extent2D viewportExtent = (_shadingModel == EShadingModel::Forward)
                                          ? _forwardPipeline->getViewportExtent()
                                          : _deferredPipeline->getViewportExtent();

        FRender2dContext render2dCtx{
            .cmdBuf       = cmdBuf.get(),
            .windowWidth  = viewportExtent.width,
            .windowHeight = viewportExtent.height,
            .cam          = {
                         .position       = input.cameraPos,
                         .view           = input.view,
                         .projection     = input.projection,
                         .viewProjection = input.projection * input.view,
            },
        };

        Render2D::begin(render2dCtx);

        if (input.appMode == AppMode::Drawing && input.editorLayer && input.clicked) {
            for (const auto&& [idx, p] : ut::enumerate(*input.clicked))
            {
                auto tex = idx % 2 == 0
                             ? AssetManager::get()->getTextureByName("uv1")
                             : AssetManager::get()->getTextureByName("face");
                YA_CORE_ASSERT(tex, "Texture not found");
                glm::vec2 pos;
                input.editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
                Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
            }
        }

        auto scene = input.sceneManager ? input.sceneManager->getActiveScene() : nullptr;
        if (scene) {
            // TODO: migrate billboard rendering to RenderFrameData snapshot
            const glm::vec2 screenSize(30, 30);
            const float     viewPortHeight = static_cast<float>(viewportExtent.height);
            const float     scaleFactor    = screenSize.x / viewPortHeight;

            for (const auto& [entity, billboard, transfCompp] :
                 scene->getRegistry().view<BillboardComponent, TransformComponent>().each())
            {
                auto        texture = billboard.image.hasPath() ? billboard.image.textureRef.getShared() : nullptr;
                const auto& pos     = transfCompp.getWorldPosition();

                glm::vec3 billboardToCamera = input.cameraPos - pos;
                float     distance          = glm::length(billboardToCamera);
                billboardToCamera           = glm::normalize(billboardToCamera);

                glm::vec3 forward = billboardToCamera;
                glm::vec3 worldUp = glm::vec3(0, 1, 0);
                glm::vec3 right   = glm::normalize(glm::cross(worldUp, forward));
                glm::vec3 up      = glm::cross(forward, right);

                glm::mat4 rot(1.0f);
                rot[0] = glm::vec4(right, 0.0f);
                rot[1] = glm::vec4(up, 0.0f);
                rot[2] = glm::vec4(forward, 0.0f);

                float     factor = scaleFactor * distance * 2.0f;
                glm::vec3 scale  = glm::vec3(factor, factor, 1.0f);

                glm::mat4 trans = glm::mat4(1.0);
                trans           = glm::translate(trans, pos);
                trans           = trans * rot;
                trans           = glm::scale(trans, scale);

                Render2D::makeWorldSprite(trans, texture);
            }
        }

        Render2D::onRender();
        UIManager::get()->render();
        Render2D::onRenderGUI();
        Render2D::end();
    }

    if (bViewportPassOpen) {
        if (_shadingModel == EShadingModel::Forward) {
            _forwardPipeline->endViewportPass(cmdBuf.get());
            YA_CORE_ASSERT(_forwardPipeline->viewportTexture, "Failed to get viewport texture for postprocessing");
        }
        else {
            _deferredPipeline->endViewportPass(cmdBuf.get());
            YA_CORE_ASSERT(_deferredPipeline->viewportTexture, "Failed to get deferred viewport texture");
        }
    }

    if (input.editorLayer) {
        EditorViewportContext ctx;
        ctx.bForwardPipeline         = (_shadingModel == EShadingModel::Forward);
        ctx.bPostprocessingEnabled   = isPostprocessingEnabled();
        ctx.postprocessOutputTexture = getPostprocessOutputTexture();
        ctx.viewportTexture          = (_shadingModel == EShadingModel::Forward)
                                         ? (_forwardPipeline ? _forwardPipeline->viewportTexture : nullptr)
                                         : (_deferredPipeline ? _deferredPipeline->viewportTexture : nullptr);

        // TODO: cache each frame's debug, change if modified
        if (_shadingModel == EShadingModel::Forward) {
            if (isShadowMappingEnabled()) {
                if (auto* directionalDepth = getShadowDirectionalDepthIV()) {
                    ctx.debugSpec.slots.push_back({
                        .label       = "ShadowDirectionalDepth",
                        .defaultView = directionalDepth,
                        .ownedView   = nullptr,
                        .image       = nullptr,
                    });
                }

                EditorViewportContext::DebugSpec::Group pointShadowGroup{
                    .label      = "Point Shadow Cubemap",
                    .type       = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
                    .beginIndex = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                    .groupSize  = 6,
                };
                for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
                    for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
                        if (auto* faceIV = getShadowPointFaceDepthIV(pointLightIndex, faceIndex))
                        {
                            auto slot = EditorViewportContext::ImageSlot{
                                .label       = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                                .defaultView = faceIV,
                                .ownedView   = nullptr,
                                .image       = nullptr,
                                .aspectFlags = EImageAspect::Depth,
                            };
                            ctx.debugSpec.slots.push_back(std::move(slot));
                        }
                    }
                }
                pointShadowGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - pointShadowGroup.beginIndex;
                if (pointShadowGroup.slotCount >= pointShadowGroup.groupSize) {
                    ctx.debugSpec.groups.push_back(std::move(pointShadowGroup));
                }
            }

            if (auto* scene = _app->getSceneManager()->getActiveScene()) {
                auto* resolver = _app->getResourceResolveSystem();
                // Use the read-only preview API instead of accessing SkyboxRuntimeState directly.
                if (resolver) {
                    for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
                        auto preview = resolver->getSkyboxPreview(entity);
                        if (!preview.bHasRenderableCubemap || !preview.cubemapTexture ||
                            !preview.cubemapTexture->getImageShared() || !preview.cubemapTexture->getImageView()) {
                            continue;
                        }

                        EditorViewportContext::DebugSpec::Group skyboxGroup{
                            .label      = "Skybox Cubemap",
                            .type       = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
                            .beginIndex = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                            .groupSize  = CubeFace_Count,
                        };

                        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                            auto* faceView = preview.cubemapFaceViews[faceIndex];
                            if (!faceView) {
                                continue;
                            }

                            ctx.debugSpec.slots.push_back({
                                .label       = std::format("SkyboxFace{}", faceIndex),
                                .defaultView = faceView,
                                .ownedView   = nullptr,
                                .image       = preview.cubemapTexture->getImageShared(),
                            });
                        }

                        skyboxGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - skyboxGroup.beginIndex;
                        if (skyboxGroup.slotCount >= skyboxGroup.groupSize) {
                            ctx.debugSpec.groups.push_back(std::move(skyboxGroup));
                        }
                        break; // only first skybox
                    }
                }
            }

            if (auto* viewportDepth = _forwardPipeline->viewportRT->getCurFrameBuffer()->getDepthTexture()) {
                ctx.debugSpec.slots.push_back({
                    .label       = "ViewportDepth",
                    .defaultView = viewportDepth->getImageView(),
                    .ownedView   = nullptr,
                    .image       = viewportDepth->getImageShared(),
                    .aspectFlags = EImageAspect::Depth,
                });
            }
        }
        else {
            auto& fb            = *_deferredPipeline->_gBufferRT->getCurFrameBuffer();
            ctx.debugSpec.slots = {
                {
                    .label       = "Position",
                    .defaultView = fb.getColorTexture(0)->getImageView(),
                    .ownedView   = nullptr,
                    .image       = fb.getColorTexture(0)->getImageShared(),
                },
                {
                    .label       = "Normal",
                    .defaultView = fb.getColorTexture(1)->getImageView(),
                    .ownedView   = nullptr,
                    .image       = fb.getColorTexture(1)->getImageShared(),
                },
                {
                    .label       = "AlbedoSpec",
                    .defaultView = fb.getColorTexture(2)->getImageView(),
                    .ownedView   = nullptr,
                    .image       = fb.getColorTexture(2)->getImageShared(),
                },
                {
                    .label       = "Depth",
                    .defaultView = fb.getDepthTexture()->getImageView(),
                    .ownedView   = nullptr,
                    .image       = fb.getDepthTexture()->getImageShared(),
                    .aspectFlags = EImageAspect::Depth,
                    .tint        = {1, 0, 0, 1}, // only red mask
                },
            };

            auto viewPortRT = _deferredPipeline->_viewportRT->getCurFrameBuffer();
            ctx.debugSpec.slots.push_back({
                .label       = "ViewPortColor0",
                .defaultView = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageView() : nullptr,
                .ownedView   = nullptr,
                .image       = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageShared() : nullptr,
            });
            ctx.debugSpec.slots.push_back({
                .label       = "ViewportDepth",
                .defaultView = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageView() : nullptr,
                .ownedView   = nullptr,
                .image       = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageShared() : nullptr,
                .aspectFlags = EImageAspect::Depth,
                .tint        = {1, 0, 0, 1}, // only red mask

            });
        }

        if (_sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView()) {
            ctx.debugSpec.slots.push_back({
                .label       = "PBR_BRDF_LUT",
                .defaultView = _sharedResources.pbrLUT->getImageView(),
                .ownedView   = nullptr,
                .image       = _sharedResources.pbrLUT->getImageShared(),
            });
        }

        if (auto* scene = _app->getSceneManager()->getActiveScene()) {
            auto* resolver       = _app->getResourceResolveSystem();
            auto* textureFactory = _render ? _render->getTextureFactory() : nullptr;
            if (resolver && textureFactory) {
                for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
                    (void)elc;
                    auto preview = resolver->getEnvironmentLightingPreview(entity);
                    if (!preview.bHasPrefilterMap || !preview.prefilterTexture ||
                        !preview.prefilterTexture->getImageShared() || !preview.prefilterTexture->getImageView()) {
                        continue;
                    }

                    auto prefilterImage = preview.prefilterTexture->getImageShared();
                    if (!prefilterImage) {
                        continue;
                    }

                    const uint32_t mipLevels = std::max(1u, prefilterImage->getMipLevels());
                    EditorViewportContext::DebugSpec::Group prefilterGroup{
                        .label      = "Environment Prefilter Cubemap",
                        .type       = EditorViewportContext::DebugSpec::EGroupType::CubeMapMipFaces,
                        .beginIndex = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                        .groupSize  = CubeFace_Count,
                    };
                    prefilterGroup.itemLabels.reserve(mipLevels);

                    for (uint32_t mipIndex = 0; mipIndex < mipLevels; ++mipIndex) {
                        const float roughness = mipLevels <= 1 ? 0.0f : static_cast<float>(mipIndex) / static_cast<float>(mipLevels - 1);
                        prefilterGroup.itemLabels.push_back(std::format("Mip {} (Roughness {:.2f})", mipIndex, roughness));

                        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                            auto faceView = textureFactory->createImageView(
                                prefilterImage,
                                ImageViewCreateInfo{
                                    .label          = std::format("EnvironmentPrefilter_Mip_{}_Face_{}", mipIndex, faceIndex),
                                    .viewType       = EImageViewType::View2D,
                                    .aspectFlags    = EImageAspect::Color,
                                    .baseMipLevel   = mipIndex,
                                    .levelCount     = 1,
                                    .baseArrayLayer = faceIndex,
                                    .layerCount     = 1,
                                });
                            if (!faceView) {
                                continue;
                            }

                            ctx.debugSpec.slots.push_back({
                                .label       = std::format("Prefilter_Mip{}_Face{}", mipIndex, faceIndex),
                                .defaultView = faceView.get(),
                                .ownedView   = faceView,
                                .image       = prefilterImage,
                            });
                        }
                    }

                    prefilterGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - prefilterGroup.beginIndex;
                    if (prefilterGroup.slotCount >= prefilterGroup.groupSize) {
                        ctx.debugSpec.groups.push_back(std::move(prefilterGroup));
                    }
                    break; // only first environment lighting preview
                }
            }
        }

        input.editorLayer->setViewportContext(ctx);
    }

    {
        YA_PROFILE_SCOPE("Screen pass")

        RenderingInfo ri{
            .label      = "Screen",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _screenRT->getExtent().toVec2(),
            },
            .layerCount       = 1,
            .colorClearValues = {ClearValue::Black()},
            .renderTarget     = _screenRT.get(),
        };

        cmdBuf->beginRendering(ri);

        auto& imManager = ImGuiManager::get();
        imManager.beginFrame();
        if (_app) {
            _app->renderGUI(input.dt);
        }
        imManager.endFrame();
        imManager.render();

        if (_render->getAPI() == ERenderAPI::Vulkan) {
            imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
        }

        cmdBuf->endRendering(ri);
    }

    // ── Per-frame resource management ───────────────────────────────────
    // Process completed async task callbacks on the main thread
    // (e.g. GPU texture uploads after background file IO completes).
    TaskQueue::get().processMainThreadCallbacks();

    cmdBuf->end();
    _render->end(imageIndex, {cmdBuf->getHandle()});

    if (_renderDocCapture) {
        _renderDocCapture->onFrameEnd();
    }
}

ForwardRenderPipeline* RenderRuntime::getForwardPipeline() const
{
    return _forwardPipeline.get();
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (_forwardPipeline) return _forwardPipeline->bShadowMapping;
    return false;
}

bool RenderRuntime::isMirrorRenderingEnabled() const
{
    return false;
}

bool RenderRuntime::hasMirrorRenderResult() const
{
    return false;
}

IRenderTarget* RenderRuntime::getShadowDepthRT() const
{
    return _forwardPipeline && _forwardPipeline->depthRT ? _forwardPipeline->depthRT.get() : nullptr;
}

IImageView* RenderRuntime::getShadowDirectionalDepthIV() const
{
    return _forwardPipeline && _forwardPipeline->shadowDirectionalDepthIV ? _forwardPipeline->shadowDirectionalDepthIV.get() : nullptr;
}

IImageView* RenderRuntime::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (!_forwardPipeline) return nullptr;
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    auto& iv = _forwardPipeline->shadowPointFaceIVs[pointLightIndex][faceIndex];
    return iv ? iv.get() : nullptr;
}

Texture* RenderRuntime::getPostprocessOutputTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.getOutputTexture();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.getOutputTexture();
    return nullptr;
}

bool RenderRuntime::isPostprocessingEnabled() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.isEnabled();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.isEnabled();
    return false;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (_forwardPipeline) {
        return _forwardPipeline->getViewportExtent();
    }
    if (_deferredPipeline) {
        return _deferredPipeline->getViewportExtent();
    }
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_viewportRect.extent);
    }
    return {};
}

void RenderRuntime::renderGUI(float dt)
{
    if (!ImGui::CollapsingHeader("Render Runtime")) {
        return;
    }
    (void)dt;

    {
        static const char* items[] = {"Forward", "Deferred"};
        int                current = static_cast<int>(_pendingShadingModel);
        if (ImGui::Combo("Shading Model", &current, items, IM_ARRAYSIZE(items))) {
            _pendingShadingModel = static_cast<EShadingModel>(current);
        }
        if (_pendingShadingModel != _shadingModel) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
        }
    }

    if (_forwardPipeline) {
        _forwardPipeline->renderGUI();
    }
    if (_deferredPipeline) {
        _deferredPipeline->renderGUI();
    }

    if (_screenRT) {
        _screenRT->onRenderGUI();
    }

    ImGui::DragFloat("Viewport Scale", &_viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);

    if (ImGui::TreeNode("RenderDoc")) {
        bool bAvailable = _renderDocCapture && _renderDocCapture->isAvailable();
        ImGui::Text("Available: %s", bAvailable ? "Yes" : "No");
        ImGui::TextWrapped("DLL Path: %s", _renderDocConfiguredDllPath.empty() ? "<default>" : _renderDocConfiguredDllPath.c_str());
        ImGui::TextWrapped("Output Dir: %s", _renderDocConfiguredOutputDir.empty() ? "<default>" : _renderDocConfiguredOutputDir.c_str());
        if (bAvailable) {
            bool bCaptureEnabled = _renderDocCapture->isCaptureEnabled();
            if (ImGui::Checkbox("Capture Enabled", &bCaptureEnabled)) {
                _renderDocCapture->setCaptureEnabled(bCaptureEnabled);
            }

            bool bHudVisible = _renderDocCapture->isHUDVisible();
            if (ImGui::Checkbox("Show RenderDoc HUD", &bHudVisible)) {
                _renderDocCapture->setHUDVisible(bHudVisible);
            }

            ImGui::Text("Capturing: %s", _renderDocCapture->isCapturing() ? "Yes" : "No");
            ImGui::Text("Delay Frames: %u", _renderDocCapture->getDelayFrames());
            ImGui::Combo("On Capture", &_renderDocOnCaptureAction, "None\0Open Replay UI\0Open Capture Folder\0");
            ImGui::TextWrapped("Last Capture: %s", _renderDocLastCapturePath.empty() ? "<none>" : _renderDocLastCapturePath.c_str());

            bool bCanCapture = _renderDocCapture->isCaptureEnabled();
            ImGui::BeginDisabled(!bCanCapture);
            if (ImGui::Button("Capture Next Frame (F9)")) {
                _renderDocCapture->requestNextFrame();
            }
            if (ImGui::Button("Capture After 120 Frames (Ctrl+F9)")) {
                _renderDocCapture->requestAfterFrames(120);
            }
            ImGui::EndDisabled();

            if (ImGui::Button("Open Last Capture Folder") && !_renderDocLastCapturePath.empty()) {
                openDirectoryInOS(_renderDocLastCapturePath);
            }
        }
        ImGui::TreePop();
    }
}

void RenderRuntime::initActivePipeline()
{
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
        _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }
    else {
        _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
        _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }

    if (_shadingModel == EShadingModel::Forward) {
        Render2D::init(_render, ForwardRenderPipeline::LINEAR_FORMAT, ForwardRenderPipeline::DEPTH_FORMAT);
    }
    else {
        Render2D::init(_render, _deferredPipeline->LINEAR_FORMAT, _deferredPipeline->DEPTH_FORMAT);
    }
}

void RenderRuntime::shutdownActivePipeline()
{
    Render2D::destroy();

    if (_forwardPipeline) {
        _forwardPipeline->shutdown();
        _forwardPipeline.reset();
    }
    if (_deferredPipeline) {
        _deferredPipeline->shutdown();
        _deferredPipeline.reset();
    }
}

void RenderRuntime::applyPendingShadingModelSwitch()
{
    if (_pendingShadingModel == _shadingModel) {
        return;
    }
    YA_PROFILE_FUNCTION_LOG();

    if ((_forwardPipeline && _forwardPipeline->hasOpenViewportPass()) ||
        (_deferredPipeline && _deferredPipeline->hasOpenViewportPass())) {
        YA_CORE_WARN("Skipping shading-model switch while a viewport pass is still open");
        return;
    }

    YA_CORE_INFO("Switching shading model: {} -> {}",
                 _shadingModel == EShadingModel::Forward ? "Forward" : "Deferred",
                 _pendingShadingModel == EShadingModel::Forward ? "Forward" : "Deferred");

    _render->waitIdle();

    shutdownActivePipeline();
    _shadingModel = _pendingShadingModel;
    initActivePipeline();

    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        onViewportResized(_viewportRect);
    }
}

} // namespace ya
