
#include "App.h"
#include "Core/Debug/RenderDocCapture.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Editor/EditorLayer.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include "ImGuiHelper.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Resource/AssetManager.h"
#include "Resource/FontManager.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/TextureLibrary.h"

#include "Core/UI/UIManager.h"
#include "ECS/Component/2D/BillboardComponent.h"

#include "utility.cc/ranges.h"

#include <glm/gtc/matrix_transform.hpp>


namespace ya
{
extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

static enum EShadingModel {
    Forward,
    Deferred
} _shadingModel = EShadingModel::Deferred;



// forward declaration - defined in App.cpp
static void openDirectoryInOS(const std::string& filePath);


void App::onSceneViewportResized(Rect2D rect)
{
    _viewportRect     = rect;
    float aspectRatio = rect.extent.x > 0 && rect.extent.y > 0 ? rect.extent.x / rect.extent.y : 16.0f / 9.0f;
    camera.setAspectRatio(aspectRatio);

    if (_forwardPipeline) {
        _forwardPipeline->onViewportResized(rect);
    }
    if (_deferredPipeline) {
        _deferredPipeline->onViewportResized(rect);
    }
}

bool App::recreateViewPortRT(uint32_t width, uint32_t height)
{
#if FORWARD
    YA_CORE_ASSERT(_forwardPipeline, "ForwardRenderPipeline not initialized");
    return _forwardPipeline->recreateViewportRT(width, height);
#endif

    // YA_CORE_ASSERT(_deferredPipeline, "DeferredRenderPipeline not initialized");
    _deferredPipeline->_viewportRT->setExtent({.width = width, .height = height});
    return true;
}

// MARK: Init
void App::initRenderPipeline()
{
    currentRenderAPI = ERenderAPI::Vulkan;

    // WHY: validate shaders before render initialized, avoid vk init(about > 3s) but shader error
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();
    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);

    // Register Slang processor for .slang shader files (hot-reload supported)
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
    // _shaderStorage->validate(ShaderDesc{.shaderName = "Test/PhongLit.glsl"}); // macro defines by various material system, so validate only, load when create pipeline
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/DebugRender.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Skybox.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/CombinedShadowMappingGenerate.glsl"});
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});
    // _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit.slang"});
    // _shaderStorage->validate(ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"});
    // _shaderStorage->validate(ShaderDesc{.shaderName = "DeferredRender/GBufferPass.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "DeferredRender/GBufferPass.slang"});
    _shaderStorage->load(ShaderDesc{.shaderName = "DeferredRender/LightPass.slang"});
    _shaderStorage->load(ShaderDesc{.shaderName = "DebugChannelExtract.comp.glsl"});

    // MARK: Render doc hook
    if (_ci.bEnableRenderDoc) {
        // before render init to hook apis
        _renderDocCapture             = ya::makeShared<RenderDocCapture>();
        _renderDocConfiguredDllPath   = _ci.renderDocDllPath;
        _renderDocConfiguredOutputDir = _ci.renderDocCaptureOutputDir;
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
            .imageFormat   = COLOR_FORMAT,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

#if FORWARD
    _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
#else
    _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
#endif

    // Get window size
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);
    _windowSize.x = static_cast<float>(winW);
    _windowSize.y = static_cast<float>(winH);

    if (_ci.bEnableRenderDoc) {
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

    // MARK: Resources
    {
        TextureLibrary::get().init();

        // Priority order: higher = cleared first (GPU resources before CPU resources)
        ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100); // GPU meshes first
        ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);      // GPU textures
        ResourceRegistry::get().registerCache(FontManager::get(), 80);          // Font textures
        ResourceRegistry::get().registerCache(AssetManager::get(), 70);         // General assets
    }


#if FORWARD
    _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
        .render  = _render,
        .windowW = winW,
        .windowH = winH,
    });
#else
    _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
        .render  = _render,
        .windowW = winW,
        .windowH = winH,
    });
#endif

    recreateViewPortRT(winW, winH);

    // Initialize Render2D at App level (shared between forward & deferred pipelines)
#if FORWARD
    Render2D::init(_render, ForwardRenderPipeline::COLOR_FORMAT, ForwardRenderPipeline::DEPTH_FORMAT);
#else
    Render2D::init(_render, _deferredPipeline->COLOR_FORMAT, _deferredPipeline->DEPTH_FORMAT);
#endif

    // Screen RT (swapchain target for ImGui/editor)
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
}

// MARK: Shutdown
void App::shutdownRenderPipeline()
{
    // Destroy Render2D before pipeline shutdown
    Render2D::destroy();

    ImGuiManager::get().shutdown();

    if (_screenRT) {
        _screenRT->destroy();
        _screenRT.reset();
    }

    _screenRenderPass.reset();

    if (_forwardPipeline) {
        _forwardPipeline->shutdown();
        _forwardPipeline.reset();
    }
    if (_deferredPipeline) {
        _deferredPipeline->shutdown();
        _deferredPipeline.reset();
    }

    if (_renderDocCapture) {
        _renderDocCapture->shutdown();
        _renderDocCapture.reset();
    }

    // Unified cleanup of all resource caches in priority order
    ResourceRegistry::get().clearAll();

    if (_render) {
        _render->waitIdle();
        _commandBuffers.clear();

        _render->destroy();
        delete _render;
        _render = nullptr;
    }
}


// MARK: Tick
void App::tickRenderPipeline(float dt)
{
    YA_PROFILE_FUNCTION()
    auto render = getRender();


    if (_windowSize.x <= 0 || _windowSize.y <= 0) {
        YA_CORE_INFO("{}x{}: Window minimized, skipping frame", _windowSize.x, _windowSize.y);
        return;
    }

    // BUG: crash on resizing while renderdoc open
    //  How to fix it?
    if (_renderDocCapture) {
        _renderDocCapture->onFrameBegin();
    }

    // Process pending viewport resize before rendering
    if (_editorLayer) {
        Rect2D pendingRect;
        if (_editorLayer->getPendingViewportResize(pendingRect)) {
            onSceneViewportResized(pendingRect);
        }
    }

    // this can avoid bunch black mosaic when resizing
    // remove this if swapchain size == every rt's size
    _render->waitIdle();


    // ===== Get swapchain image index =====
    int32_t imageIndex = -1;
    if (!render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    auto cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();

    // MARK: Resource grab
    Entity* runtimeCamera = getPrimaryCamera();
    if (runtimeCamera && runtimeCamera->isValid())
    {
        auto cc = runtimeCamera->getComponent<CameraComponent>();
        auto tc = runtimeCamera->getComponent<TransformComponent>();

#if FORWARD
        const Extent2D ext = _forwardPipeline->getViewportExtent();
        cameraController.update(*tc, *cc, inputManager, ext, dt);
        cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
#else
        const Extent2D ext = _deferredPipeline->_viewportRT->getExtent();
        cameraController.update(*tc, *cc, inputManager, ext, dt);
        cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
#endif
    }

    glm::vec3 cameraPos;
    glm::mat4 view;
    glm::mat4 projection;
    bool      bUseRuntimeCamera = _appState == AppState::Runtime &&
                             runtimeCamera && runtimeCamera->isValid() &&
                             runtimeCamera->hasComponent<CameraComponent>();
    if (bUseRuntimeCamera) {
        auto cc    = runtimeCamera->getComponent<CameraComponent>();
        auto tc    = runtimeCamera->getComponent<TransformComponent>();
        view       = cc->getFreeView();
        projection = cc->getProjection();
        cameraPos  = tc->getWorldPosition();
    }
    else {
        view       = camera.getViewMatrix();
        projection = camera.getProjectionMatrix();
        cameraPos  = camera.getPosition();
    }

    // MARK: Shading
#if FORWARD
    // Forward
    _forwardPipeline->tick(ForwardRenderPipeline::TickDesc{
        .cmdBuf                   = cmdBuf.get(),
        .dt                       = dt,
        .view                     = view,
        .projection               = projection,
        .cameraPos                = cameraPos,
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .appMode                  = static_cast<int>(_appMode),
        .clicked                  = &clicked,
    });
#else
    // Deferred
    _deferredPipeline->tick(DeferredRenderPipeline::TickDesc{
        .cmdBuf                   = cmdBuf.get(),
        .dt                       = dt,
        .view                     = view,
        .projection               = projection,
        .cameraPos                = cameraPos,
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .appMode                  = static_cast<int>(_appMode),
        .clicked                  = &clicked,
    });
#endif

    // MARK: Render2D (shared between forward & deferred pipelines)
    {
        YA_PROFILE_SCOPE("Render2D")

#if FORWARD
        const Extent2D viewportExtent = _forwardPipeline->getViewportExtent();
#else
        const Extent2D viewportExtent = _deferredPipeline->getViewportExtent();
#endif

        FRender2dContext render2dCtx{
            .cmdBuf       = cmdBuf.get(),
            .windowWidth  = viewportExtent.width,
            .windowHeight = viewportExtent.height,
            .cam          = {

                .position       = cameraPos,
                .view           = view,
                .projection     = projection,
                .viewProjection = projection * view,
            },
        };

        Render2D::begin(render2dCtx);

        // Editor sprite placement
        if (_appMode == AppMode::Drawing && _editorLayer) {
            for (const auto&& [idx, p] : ut::enumerate(clicked))
            {
                auto tex = idx % 2 == 0
                             ? AssetManager::get()->getTextureByName("uv1")
                             : AssetManager::get()->getTextureByName("face");
                YA_CORE_ASSERT(tex, "Texture not found");
                glm::vec2 pos;
                _editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
                Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
            }
        }

        // Billboard components
        auto scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr;
        if (scene) {
            const glm::vec2 screenSize(30, 30);
            const float     viewPortHeight = static_cast<float>(viewportExtent.height);
            const float     scaleFactor    = screenSize.x / viewPortHeight;

            for (const auto& [entity, billboard, transfCompp] :
                 scene->getRegistry().view<BillboardComponent, TransformComponent>().each())
            {
                auto        texture = billboard.image.hasPath() ? billboard.image.textureRef.getShared() : nullptr;
                const auto& pos     = transfCompp.getWorldPosition();

                glm::vec3 billboardToCamera = cameraPos - pos;
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

    // End viewport rendering pass (also runs postprocessing for forward pipeline)
#if FORWARD
    _forwardPipeline->endViewportPass(cmdBuf.get());
    YA_CORE_ASSERT(_forwardPipeline->viewportTexture, "Failed to get viewport texture for postprocessing");
#else
    _deferredPipeline->endViewportPass(cmdBuf.get());
    YA_CORE_ASSERT(_deferredPipeline->_viewportRT->getCurFrameBuffer()->getColorTexture(0), "Failed to get viewport texture");
#endif

    // Set viewport context for editor layer before ImGui render
    if (_editorLayer) {
        EditorViewportContext ctx;
#if FORWARD
        ctx.viewportTexture           = _forwardPipeline->viewportTexture;
        ctx.bPostprocessingEnabled    = isPostprocessingEnabled();
        ctx.postprocessOutputTexture  = getPostprocessOutputTexture();
        ctx.bShadowMappingEnabled     = isShadowMappingEnabled();
        ctx.shadowDepthRT             = getShadowDepthRT();
        ctx.shadowDirectionalDepthIV  = getShadowDirectionalDepthIV();
        ctx.getShadowPointFaceDepthIV = [this](uint32_t pointLightIndex, uint32_t faceIndex) -> IImageView* {
            return getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
        };
        ctx.bMirrorRenderResult = hasMirrorRenderResult();
        ctx.mirrorRenderTarget  = nullptr; // getMirrorRenderTarget();
#else
        ctx.viewportTexture = _deferredPipeline->_viewportRT->getCurFrameBuffer()->getColorTexture(0);
        ctx.deferredSpec    = {
               .gBufferPostion        = _deferredPipeline->_gBufferRT->getCurFrameBuffer()->getColorTexture(0)->getImageView(),
               .gBufferNormal         = _deferredPipeline->_gBufferRT->getCurFrameBuffer()->getColorTexture(1)->getImageView(),
               .gBufferAlbedoSpecular = _deferredPipeline->_gBufferRT->getCurFrameBuffer()->getColorTexture(2)->getImageView(),
               .gBufferSpecular       = _deferredPipeline->_debugSpecularIV,
        };
#endif
        _editorLayer->setViewportContext(ctx);
    }

    // MARK: Editor pass
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
            //
            .renderTarget = _screenRT.get(),
        };

        cmdBuf->beginRendering(ri);

        // Render ImGui
        auto& imManager = ImGuiManager::get();
        imManager.beginFrame();
        {
            this->renderGUI(dt);
        }
        imManager.endFrame();
        imManager.render();

        if (render->getAPI() == ERenderAPI::Vulkan) {
            // record render cmds in fact, not submit
            imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
        }

        cmdBuf->endRendering(ri);
    }
    cmdBuf->end();

    // TODO: multi-thread rendering
    // ===== Single Submit: Wait on imageAvailable, Signal renderFinished, Set fence =====
    // render->submitToQueue(
    //     {cmdBuf->getHandle()},
    //     {render->getCurrentImageAvailableSemaphore()},    // Wait for swapchain image
    //     {render->getRenderFinishedSemaphore(imageIndex)}, // Signal when all rendering done
    //     render->getCurrentFrameFence());                  // Signal fence when done

    // // ===== Present: Wait on renderFinished =====
    // int result = render->presentImage(imageIndex, {render->getRenderFinishedSemaphore(imageIndex)});

    // // Check for swapchain recreation needed
    // if (result == 2 /* VK_SUBOPTIMAL_KHR */) {
    //     YA_CORE_INFO("Swapchain suboptimal detected in App, will recreate next frame");
    // }
    // // Advance to next frame
    // render->advanceFrame();
    render->end(imageIndex, {cmdBuf->getHandle()});

    if (_renderDocCapture) {
        _renderDocCapture->onFrameEnd();
    }
} // namespace ya


// MARK: Render resource query proxies
ForwardRenderPipeline* App::getForwardPipeline() const
{
    return _forwardPipeline.get();
}

bool App::isShadowMappingEnabled() const
{
    return _forwardPipeline ? _forwardPipeline->bShadowMapping : false;
}

bool App::isMirrorRenderingEnabled() const
{
    return false;
}

bool App::hasMirrorRenderResult() const
{
    return false;
}

IRenderTarget* App::getShadowDepthRT() const
{
    return _forwardPipeline && _forwardPipeline->depthRT ? _forwardPipeline->depthRT.get() : nullptr;
}

IImageView* App::getShadowDirectionalDepthIV() const
{
    return _forwardPipeline && _forwardPipeline->shadowDirectionalDepthIV ? _forwardPipeline->shadowDirectionalDepthIV.get() : nullptr;
}

IImageView* App::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (!_forwardPipeline) return nullptr;
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    auto& iv = _forwardPipeline->shadowPointFaceIVs[pointLightIndex][faceIndex];
    return iv ? iv.get() : nullptr;
}
Texture* App::getPostprocessOutputTexture() const
{
    return _forwardPipeline && _forwardPipeline->postprocessTexture ? _forwardPipeline->postprocessTexture.get() : nullptr;
}


bool App::isPostprocessingEnabled() const
{
    return _forwardPipeline && _forwardPipeline->basicPostprocessingSystem && _forwardPipeline->basicPostprocessingSystem->bEnabled;
}

void App::renderGUI(float dt)
{
    _editorLayer->onImGuiRender([this, dt]() {
        this->onRenderGUI(dt);
    });
}

void App::renderGUIRenderPipeline()
{
    if (_forwardPipeline) {
        _forwardPipeline->renderGUI();
    }
    if (_deferredPipeline) {
        _deferredPipeline->renderGUI();
    }
}

} // namespace ya