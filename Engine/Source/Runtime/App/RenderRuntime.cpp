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

#include "Core/Async/TaskQueue.h"

#include "Core/UI/UIManager.h"
#include "ECS/Component/2D/BillboardComponent.h"

#include "utility.cc/ranges.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

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
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/Unlit.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/SimpleMaterial.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Sprite2D_Screen.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Sprite2D_World.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/DebugRender.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Skybox.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/CombinedShadowMappingGenerate.glsl"});
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});
    // _shaderStorage->load(ShaderDesc{.shaderName = "DeferredRender/GBufferPass.slang"});
    // _shaderStorage->load(ShaderDesc{.shaderName = "DeferredRender/LightPass.slang"});
    // _shaderStorage->load(ShaderDesc{.shaderName = "DebugChannelExtract.comp.glsl"});

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
        _skyboxDSL = IDescriptorSetLayout::create(
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

        _skyboxDSP = IDescriptorPool::create(
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

        _skyboxSampler = Sampler::create(SamplerDesc{
            .label        = "App_SkyboxSampler",
            .addressModeU = ESamplerAddressMode::Repeat,
            .addressModeV = ESamplerAddressMode::Repeat,
            .addressModeW = ESamplerAddressMode::Repeat,
        });

        _fallbackSkyboxTexture = Texture::createSolidCubeMap(ColorU8_t{0, 0, 0, 255}, "App_FallbackSkybox");
        YA_CORE_ASSERT(_fallbackSkyboxTexture && _fallbackSkyboxTexture->getImageView(),
                       "Failed to create fallback skybox cubemap");

        _fallbackSkyboxDS = _skyboxDSP->allocateDescriptorSets(_skyboxDSL);
        _render->getDescriptorHelper()->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(
                    _fallbackSkyboxDS,
                    0,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    {
                        DescriptorImageInfo(
                            _fallbackSkyboxTexture->getImageView()->getHandle(),
                            _skyboxSampler->getHandle(),
                            EImageLayout::ShaderReadOnlyOptimal),
                    }),
            },
            {});
    }

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
    }

    _render->allocateCommandBuffers(_render->getSwapchainImageCount(), _commandBuffers);
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
    shutdownActivePipeline();

    ImGuiManager::get().shutdown();

    if (_screenRT) {
        _screenRT->destroy();
        _screenRT.reset();
    }

    _screenRenderPass.reset();

    if (_renderDocCapture) {
        _renderDocCapture->shutdown();
        _renderDocCapture.reset();
    }

    ResourceRegistry::get().clearAll();

    _fallbackSkyboxTexture.reset();
    _fallbackSkyboxDS = nullptr;
    _skyboxSampler.reset();
    _skyboxDSP.reset();
    _skyboxDSL.reset();

    if (_render) {
        _render->waitIdle();

        // GPU is idle — safe to execute ALL pending resource destructors now.
        DeferredDeletionQueue::get().flushAll();

        // Stop async task queue (join worker threads).
        TaskQueue::get().stop();

        _commandBuffers.clear();
        _render->destroy();
        delete _render;
        _render = nullptr;
    }
}

void RenderRuntime::resetSkyboxPool()
{
    if (!_skyboxDSP || !_skyboxDSL) {
        return;
    }

    // Return ALL descriptor sets back to the pool (including the fallback DS).
    _skyboxDSP->resetPool();
    _fallbackSkyboxDS = nullptr;

    // Re-allocate the permanent fallback descriptor set.
    _fallbackSkyboxDS = _skyboxDSP->allocateDescriptorSets(_skyboxDSL);
    YA_CORE_ASSERT(_fallbackSkyboxDS, "Failed to re-allocate fallback skybox descriptor set");

    if (_fallbackSkyboxTexture && _fallbackSkyboxTexture->getImageView() && _skyboxSampler) {
        _render->getDescriptorHelper()->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(
                    _fallbackSkyboxDS,
                    0,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    {
                        DescriptorImageInfo(
                            _fallbackSkyboxTexture->getImageView()->getHandle(),
                            _skyboxSampler->getHandle(),
                            EImageLayout::ShaderReadOnlyOptimal),
                    }),
            },
            {});
    }
}

// MARK: render
void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_FUNCTION()

    applyPendingShadingModelSwitch();

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

            if (auto* viewportDepth = _forwardPipeline->viewportRT->getCurFrameBuffer()->getDepthTexture()) {
                ctx.debugSpec.slots.push_back({
                    .label       = "ViewportDepth",
                    .defaultView = viewportDepth->getImageView(),
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
                    .image       = fb.getColorTexture(0)->getImageShared(),
                },
                {
                    .label       = "Normal",
                    .defaultView = fb.getColorTexture(1)->getImageView(),
                    .image       = fb.getColorTexture(1)->getImageShared(),
                },
                {
                    .label       = "AlbedoSpec",
                    .defaultView = fb.getColorTexture(2)->getImageView(),
                    .image       = fb.getColorTexture(2)->getImageShared(),
                },
                {
                    .label       = "Depth",
                    .defaultView = fb.getDepthTexture()->getImageView(),
                    .image       = fb.getDepthTexture()->getImageShared(),
                    .aspectFlags = EImageAspect::Depth,
                    .tint        = {1, 0, 0, 1}, // only red mask
                },
            };

            auto viewPortRT = _deferredPipeline->_viewportRT->getCurFrameBuffer();
            ctx.debugSpec.slots.push_back({
                .label       = "ViewPortColor0",
                .defaultView = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageView() : nullptr,
                .image       = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageShared() : nullptr,
            });
            ctx.debugSpec.slots.push_back({
                .label       = "ViewportDepth",
                .defaultView = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageView() : nullptr,
                .image       = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageShared() : nullptr,
                .aspectFlags = EImageAspect::Depth,
                .tint        = {1, 0, 0, 1}, // only red mask

            });
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
