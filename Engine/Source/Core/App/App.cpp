#include "App.h"


// Core
#include "Core/App/App.h"
#include "Core/App/SDLMisc.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"
#include "Core/System/FileWatcher.h"
#include "Core/System/VirtualFileSystem.h"



// Managers/System
#include "Core/Manager/Facade.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "ECS/System/TransformSystem.h"
#include "ImGuiHelper.h"
#include "Render/Core/FrameBuffer.h"
#include "Scene/SceneManager.h"

#include "Resource/AssetManager.h"
#include "Resource/PrimitiveMeshCache.h"
// #include "Resource/RenderTargetPool.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/TextureLibrary.h"


// Render Core
#include "Render/Core/Texture.h"



// ECS
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/MirrorComponent.h"
#include "ECS/Component/PlayerComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/2D/UIComponentSytstem.h"
#include "ECS/System/LuaScriptingSystem.h"
#include "ECS/System/PhongMaterialSystem.h"
#include "ECS/System/SimpleMaterialSystem.h"
#include "ECS/System/UnlitMaterialSystem.h"



// Render
#include "Platform/Render/Vulkan//VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"
#include "Render/Pipelines/BasicPostprocessing.h"
#include "Render/Render.h"



// UI
#include "Core/UI/UIButton.h"
#include "Core/UI/UIManager.h"
#include "Core/UI/UIPanel.h"
#include "Core/UI/UITextBlock.h"

// Utility
#include "utility.cc/ranges.h"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// SDL
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>


// Scene
#include "Scene/Scene.h"



std::vector<glm::vec2> clicked;



namespace ya
{

// Define the static member variable
App     *App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;



void App::onSceneViewportResized(Rect2D rect)
{
    _viewportRect     = rect;
    float aspectRatio = rect.extent.x > 0 && rect.extent.y > 0 ? rect.extent.x / rect.extent.y : 16.0f / 9.0f;
    camera.setAspectRatio(aspectRatio);

    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    _viewportRT->setExtent(newExtent);

    // TODO: this should just be a framebuffer?
    //      but framebuffer depend on the renderpass
    // Recreate postprocess image when viewport size changes
    if (_render && newExtent.width > 0 && newExtent.height > 0) {
        auto vkRender = _render->as<VulkanRender>();

        // Wait for GPU to finish using old resources before destroying them
        if (_postprocessTexture) {
            _render->waitIdle();
        }

        // Release old texture
        _postprocessTexture.reset();

        // Create new postprocess image
        ImageCreateInfo imageCI{
            .label  = "PostprocessImage",
            .format = EFormat::R8G8B8A8_UNORM,
            .extent = {
                .width  = newExtent.width,
                .height = newExtent.height,
                .depth  = 1,
            },
            .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .initialLayout = EImageLayout::Undefined,
        };
        auto postprocessImage = VulkanImage::create(vkRender, imageCI);

        if (postprocessImage) {
            auto postprocessImageView = VulkanImageView::create(
                vkRender,
                postprocessImage,
                VK_IMAGE_ASPECT_COLOR_BIT);
            postprocessImageView->setDebugName("PostprocessImageView");

            // 使用新的 Texture 构造函数包装 IImage/IImageView
            _postprocessTexture = std::make_shared<Texture>(postprocessImage, postprocessImageView, "PostprocessTexture");
        }
    }
}

// ===== TODO: These global variables should be moved to appropriate managers =====



ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);



void App::init(AppDesc ci)
{
    YA_PROFILE_FUNCTION_LOG();
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    // register terminal  C-c signal
    handleSystemSignals();


    {
        YA_PROFILE_SCOPE_LOG("App Init Subsystems");
        {
            YA_PROFILE_SCOPE_LOG("Static Initializers");
            profiling::StaticInitProfiler::recordStart();
            ClassRegistry::instance().executeAllPostStaticInitializers();
            profiling::StaticInitProfiler::recordEnd();
        }
        Logger::init();
        VirtualFileSystem::init();
        FileWatcher::init();
        // 反射系统已通过静态初始化自动注册（YA_REFLECT 宏）
        MaterialFactory::init();
    }

    currentRenderAPI = ERenderAPI::Vulkan;

    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();

    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    _shaderStorage->load(ShaderDesc{
        .shaderName = "Test/Unlit.glsl",
    });
    _shaderStorage->load(ShaderDesc{
        .shaderName = "Test/SimpleMaterial.glsl",
    });
    _shaderStorage->load(ShaderDesc{
        .shaderName = "Sprite2D.glsl",
    });
    _shaderStorage->load(ShaderDesc{
        .shaderName = "Test/PhongLit.glsl", // 使用新版带 type 和 cutOff 的 shader
    });
    _shaderStorage->load(ShaderDesc{
        .shaderName = "PostProcessing/Basic.glsl", // 使用新版带 type 和 cutOff 的 shader
    });


    // ===== Initialize Render =====
    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };
    FPSControl::get()->bEnable = true;
    FPSControl::get()->setFPSLimit(120.f);

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

    // Get window size
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);
    _windowSize.x = static_cast<float>(winW);
    _windowSize.y = static_cast<float>(winH);

    // Allocate command buffers for swapchain (both scene and UI in same buffer)
    _render->allocateCommandBuffers(_render->getSwapchainImageCount(), _commandBuffers);

    constexpr auto _sampleCount = ESampleCount::Sample_1; // TODO: support MSAA

    // MARK: Viewport pass (dynamic rendering only)
    _viewportRenderPass = nullptr;

    _viewportRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = static_cast<uint32_t>(winW), .height = static_cast<uint32_t>(winH)},
        .frameBufferCount = 1,
        .attachments      = {
                 .colorAttach = {
                AttachmentDescription{
                         .index          = 0,
                         .format         = EFormat::R8G8B8A8_UNORM,
                         .samples        = _sampleCount,
                         .loadOp         = EAttachmentLoadOp::Clear,
                         .storeOp        = EAttachmentStoreOp::Store,
                         .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                         .stencilStoreOp = EAttachmentStoreOp::DontCare,
                         .initialLayout  = EImageLayout::ShaderReadOnlyOptimal,
                         .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                         .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
                 .depthAttach = AttachmentDescription{
                     .index          = 1,
                     .format         = DEPTH_FORMAT,
                     .samples        = _sampleCount,
                     .loadOp         = EAttachmentLoadOp::Clear,
                     .storeOp        = EAttachmentStoreOp::Store,
                     .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                     .stencilStoreOp = EAttachmentStoreOp::DontCare,
                     .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                     .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                     .usage          = EImageUsage::DepthStencilAttachment,
            },
        },
    });

    // pre construct all material systems
    addMaterialSystem<SimpleMaterialSystem>(nullptr,
                                            PipelineRenderingInfo{
                                                .label                   = "SimpleMaterial Pipeline",
                                                .viewMask                = 0,
                                                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                                                .depthAttachmentFormat   = DEPTH_FORMAT,
                                                .stencilAttachmentFormat = EFormat::Undefined,
                                            });
    addMaterialSystem<UnlitMaterialSystem>(nullptr,
                                           PipelineRenderingInfo{
                                               .label                   = "UnlitMaterialSystem Pipeline",
                                               .viewMask                = 0,
                                               .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                                               .depthAttachmentFormat   = DEPTH_FORMAT,
                                               .stencilAttachmentFormat = EFormat::Undefined,
                                           });
    addMaterialSystem<PhongMaterialSystem>(nullptr,
                                           PipelineRenderingInfo{
                                               .label                   = "PhongMaterial Pipeline",
                                               .viewMask                = 0,
                                               .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                                               .depthAttachmentFormat   = DEPTH_FORMAT,
                                               .stencilAttachmentFormat = EFormat::Undefined,
                                           });
    _deleter.push("MaterialSystems", [this](void *) {
        _materialSystems.clear();
    });

    // MARK: tex-> Postprocessing
    {
        auto            vkRender = _render->as<VulkanRender>();
        ImageCreateInfo imageCI{
            .label  = "PostprocessImage",
            .format = EFormat::R8G8B8A8_UNORM,
            .extent = {
                .width  = static_cast<uint32_t>(winW),
                .height = static_cast<uint32_t>(winH),
                .depth  = 1,
            },
            .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .initialLayout = EImageLayout::Undefined,
        };
        auto postprocessImage = VulkanImage::create(vkRender, imageCI);

        if (postprocessImage) {
            auto postprocessImageView = VulkanImageView::create(
                vkRender,
                postprocessImage,
                VK_IMAGE_ASPECT_COLOR_BIT);
            postprocessImageView->setDebugName("PostprocessImageView");

            // 使用新的 Texture 构造函数包装 IImage/IImageView
            _postprocessTexture = std::make_shared<Texture>(postprocessImage, postprocessImageView, "PostprocessTexture");
        }
        _deleter.push("PostprocessTexture", [this](void *) {
            _postprocessTexture.reset();
        });
    }
    // auto _postProcessingRT = ya::createRenderTarget(RenderTargetCreateInfo{
    //     .label       = "Postprocess RenderTarget",
    //     .colorFormat = EFormat::R8G8B8A8_UNORM,
    //     .depthFormat = EFormat::Undefined,
    //     .extent      = {
    //              .width  = static_cast<uint32_t>(winW),
    //              .height = static_cast<uint32_t>(winH),
    //     },
    //     .bSwapChainTarget = false,
    //     .frameBufferCount = 1,
    // });
    // MARK: temp mirror texture
    {
        _mirrorRT = ya::createRenderTarget(RenderTargetCreateInfo{
            .label            = "Postprocess RenderTarget",
            .renderingMode    = ERenderingMode::DynamicRendering,
            .bSwapChainTarget = false,
            .extent           = {
                          .width  = static_cast<uint32_t>(winW),
                          .height = static_cast<uint32_t>(winH),
            },
            .frameBufferCount = 1,
            .attachments      = {

                .colorAttach = {
                    AttachmentDescription{
                        .index          = 0,
                        .format         = EFormat::R8G8B8A8_UNORM,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::Undefined,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal, // For sampling
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },
                },
                .depthAttach = AttachmentDescription{
                    .index          = 1,
                    .format         = DEPTH_FORMAT,
                    .samples        = _sampleCount,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::Undefined,
                    .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                    .usage          = EImageUsage::DepthStencilAttachment,
                },
            },
        });
        _deleter.push("MirrorRT", [this](void *) {
            _mirrorRT.reset();
        });
    }

    // MARK: Screen pass (dynamic rendering only)
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
            Extent2D newExtent{
                .width  = now.extent.width,
                .height = now.extent.height,
            };

            if (bImageRecreated) {
                // _viewportRT->setExtent(newExtent); // see App::onSceneViewportResized
                _screenRT->setExtent(newExtent);
            }
            if ((now.extent.width != old.extent.width ||
                 now.extent.height != old.extent.height ||
                 old.presentMode != now.presentMode))
            {
                // _viewportRT->setExtent(newExtent); // see App::onSceneViewportResized
                _screenRT->setExtent(newExtent);
            }
        });



    // FIXME: current 2D rely on the the white texture of App, fix dependencies and move before load scene
    // Initialize Render2D for dynamic rendering (depthTestEnable=false allows UI pass without depth)
    Render2D::init(_render);
    Render2D::data._systems.push_back(makeShared<UIComponentSystem>());

    // MARK: Imgui
    auto &imManager = ImGuiManager::get();
    imManager.init(_render, nullptr);



    // MARK: resource cache
    TextureLibrary::get().init();

    // Register all resource caches with ResourceRegistry for unified cleanup
    // Priority order: higher = cleared first (GPU resources before CPU resources)
    ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100); // GPU meshes first
    ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);      // GPU textures
    ResourceRegistry::get().registerCache(FontManager::get(), 80);          // Font textures
    ResourceRegistry::get().registerCache(AssetManager::get(), 70);         // General assets

    // MARK: Initialize RenderTargetPool for dynamic render target allocation
    // RenderTargetPool::get().init(_render);
    // ResourceRegistry::get().registerCache(&RenderTargetPool::get(), 95); // High priority - after meshes but before textures



    {
        YA_PROFILE_SCOPE_LOG("Inheritance Init");
        onInit(ci);
    }



    // ===== Initialize SceneManager =====
    _sceneManager = new SceneManager();
    // _sceneManager->onSceneInit.addLambda(this, [this](Scene *scene) { this->onSceneInit(scene); });
    _sceneManager->onSceneDestroy.addLambda(this, [this](Scene *scene) { this->onSceneDestroy(scene); });
    _sceneManager->onSceneActivated.addLambda(this, [this](Scene *scene) { this->onSceneActivated(scene); });

    // wait something done
    _render->waitIdle();

    {
        YA_PROFILE_SCOPE_LOG("Post Init");
        onPostInit();
    }

    _editorLayer = new EditorLayer(this);
    _editorLayer->onAttach();
    // todo: use ref
    // _editorLayer->onViewportResized.set([this](Rect2D rect) {
    //     onSceneViewportResized(rect);
    // });

    // see TypeRenderer.h
    ya::registerBuiltinTypeRenderers();

    // _viewportRT->onFrameBufferRecreated.addLambda(this, [this]() {
    //     YA_CORE_INFO("EditorLayer: viewport RT recreated, cleaning up ImGui texture cache");
    //     auto imageView = _viewportRT->getFrameBuffer()->getImageView(0);
    //     _editorLayer->setViewportImage(imageView);
    // });
    _luaScriptingSystem = new LuaScriptingSystem();
    _luaScriptingSystem->init();

    loadScene(ci.defaultScenePath);

    camera.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
}

template <typename T>
int App::dispatchEvent(const T &event)
{
    if (0 == onEvent(event)) {
        MessageBus::get()->publish(event);
    }
    return 0;
}

void App::renderGUI(float dt)
{
    _editorLayer->onImGuiRender([this, dt]() {
        this->onRenderGUI(dt);
    });
}

// MARK: on init
void App::onInit(AppDesc ci)
{
    // auto &bus = *MessageBus::get();
    FontManager::get()->loadFont("Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 48);

    // auto panel     = UIFactory::create<UIPanel>();
    // panel->_color  = FUIColor(0.2f, 0.2f, 0.2f, 0.8f);
    // auto btn       = UIFactory::create<UIButton>();
    // btn->_position = {50.0f, 50.0f};
    // btn->_size     = {200.0f, 100.0f};
    // panel->addChild(btn);

    // auto textBlock   = UIFactory::create<UITextBlock>();
    // textBlock->_font = FontManager::get()->getFont("JetBrainsMono-Medium", 48).get();
    // btn->addChild(textBlock);


    auto mgr = UIManager::get();
    // mgr->_rootCanvas->addChild(panel);
}

void App::onPostInit()
{
    // those resources are depends on the render context
    // LOAD demo testing textures
    const char *faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char *uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTexture("face", faceTexturePath);
    ya::AssetManager::get()->loadTexture("uv1", uv1TexturePath);

    onScenePostInit.broadcast();
}


int App::onEvent(const Event &event)
{
    // YA_CORE_TRACE("Event processed: {}", event.type);
    EventProcessState ret = ImGuiManager::get().processEvent(event);
    if (ret != EventProcessState::Continue) {
        return 0; // ImGui captured the event
    }


    bool      bHandled = false;
    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
    {
        bHandled |= onMouseMoved(static_cast<const MouseMoveEvent &>(event));

    } break;
    case EEvent::MouseButtonReleased:
    {
        bHandled |= onMouseButtonReleased(static_cast<const MouseButtonReleasedEvent &>(event));
    } break;
    case EEvent::WindowResize:
    {
        bHandled |= onWindowResized(static_cast<const WindowResizeEvent &>(event));
    } break;
    case EEvent::KeyReleased:
    {
        bHandled |= onKeyReleased(static_cast<const KeyReleasedEvent &>(event));
    } break;
    case EEvent::MouseScrolled:
    {
        bHandled |= onMouseScrolled(static_cast<const MouseScrolledEvent &>(event));
    } break;
    case EEvent::None:
        break;
    case EEvent::WindowClose:
        requestQuit();
        break;
    case EEvent::WindowRestore:
        _bMinimized = false;
        break;
    case EEvent::WindowMinimize:
        _bMinimized = true;
        break;
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    case EEvent::WindowMoved:
    case EEvent::AppTick:
    case EEvent::AppUpdate:
    case EEvent::AppRender:
        break;
    case EEvent::AppQuit:
        requestQuit();
        break;
    case EEvent::KeyPressed:
    case EEvent::KeyTyped:
    case EEvent::MouseButtonPressed:
    case EEvent::EventTypeCount:
    case EEvent::ENUM_MAX:
        break;
    }

    if (bHandled) {
        return 0;
    }

    inputManager.processEvent(event);


    if (bHandled) {
        return 0;
    }

    bool bInViewport = FUIHelper::isPointInRect(_lastMousePos, _viewportRect.pos, _viewportRect.extent);
    // currently ui only rendering in viewport
    if (bInViewport)
    {
        UIAppCtx ctx{
            .lastMousePos = _lastMousePos,
            .bInViewport  = bInViewport,
            .viewportRect = _viewportRect,
        };
        _editorLayer->screenToViewport(_lastMousePos, ctx.lastMousePos);
        UIManager::get()->onEvent(event, ctx);
    }

    _editorLayer->onEvent(event);

    // if()
    return 0;
}


// MARK: QUIT
void ya::App::quit()
{
    if (_render) {
        _render->waitIdle();
    }
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        onQuit();
    }

    unloadScene();
    _editorLayer->onDetach();
    delete _editorLayer;

    // CRITICAL: Destroy SceneManager BEFORE LuaScriptingSystem
    // LuaScriptComponent holds sol::object references to lua state
    // If lua state is destroyed first, component destruction will crash
    delete _sceneManager;

    delete _luaScriptingSystem;

    MaterialFactory::get()->destroy();

    Render2D::destroy();

    // Cleanup managers
    // if (ImGuiManager::get()) {
    ImGuiManager::get().shutdown();
    // }

    // Cleanup material systems (managed externally now)

    // Cleanup render targets before render passes (dependency order)
    if (_viewportRT) {
        _viewportRT->destroy();
        _viewportRT.reset();
    }

    if (_screenRT) {
        _screenRT->destroy();
        _screenRT.reset();
    }

    _screenRenderPass.reset();
    _viewportRenderPass.reset();

    _deleter.clear();
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



int ya::App::run()
{

    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    while (bRunning) {

        time_point_t now        = clock_t::now();
        auto         dtMicroSec = std::chrono::duration_cast<std::chrono::microseconds>(now - _lastTime).count();
        float        dtSec      = (float)((double)dtMicroSec / 1000000.0);
        dtSec                   = std::max(dtSec, 0.0001f);
        _lastTime               = now;


        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int ya::App::processEvent(SDL_Event &event)
{
    processSDLEvent(
        event,
        [this](const auto &e) { this->dispatchEvent(e); });
    return 0;
};


int ya::App::iterate(float dt)
{
    YA_PROFILE_FUNCTION()
    SDL_Event evt;
    SDL_PollEvent(&evt);
    processEvent(evt);

    dt += FPSControl::get()->update(dt);

    // Skip rendering when minimized to avoid swapchain recreation with invalid extent
    // TODO: only skip render, but still update logic
    if (_bMinimized) {
        SDL_Delay(100); // Small delay to reduce CPU usage when minimized
        // YA_CORE_INFO("Application minimized, skipping frame");
        return 0;
    }
    if (!_bPause) {
        tickLogic(dt);
    }
    tickRender(dt);
    taskManager.update();
    ++_frameIndex;
    return 0;
}


// MARK: update
void App::tickLogic(float dt)
{
    YA_PROFILE_FUNCTION()
    Facade.timerManager.onUpdate(dt);

    // Store real-time delta for editor
    // Note: ClearValue is now set in onRender() via beginRenderPass() or beginRendering()

    static ResourceResolveSystem *resourceResolveSystem = []() {
        auto sys = new ResourceResolveSystem();
        std::atexit([]() {
            resourceResolveSystem->destroy();
            delete resourceResolveSystem;
        });
        sys->init();
        return sys;
    }();
    static TransformSystem *transformSystem = []() {
        auto sys = new TransformSystem();
        std::atexit([]() {
            transformSystem->destroy();
            delete transformSystem;
        });
        sys->init();
        return sys;
    }();

    resourceResolveSystem->onUpdate(dt);
    transformSystem->onUpdate(dt);

    Render2D::onUpdate(dt);

    switch (_appState) {

    case AppState::Editor:
        break;
    case AppState::Simulation:
    case AppState::Runtime:
    {
        _luaScriptingSystem->onUpdate(dt);
    } break;
    }

    // 文件监视器轮询（检测文件变化）
    if (auto *watcher = FileWatcher::get()) {
        watcher->poll();
    }

    _editorLayer->onUpdate(dt);
    inputManager.postUpdate();



    inputManager.preUpdate();
    // Update Editor camera (FreeCamera)
    cameraController.update(camera, inputManager, dt);
}

// MARK: Render
void App::tickRender(float dt)
{
    YA_PROFILE_FUNCTION()
    auto render = getRender();

    // Process pending viewport resize before rendering
    if (_editorLayer) {
        Rect2D pendingRect;
        if (_editorLayer->getPendingViewportResize(pendingRect)) {
            onSceneViewportResized(pendingRect);
        }
    }
    // TODO: optimize the image recreation
    render->waitIdle();

    if (_windowSize.x <= 0 || _windowSize.y <= 0) {
        YA_CORE_INFO("{}x{}: Window minimized, skipping frame", _windowSize.x, _windowSize.y);
        return;
    }

    // ===== Get swapchain image index =====
    int32_t imageIndex = -1;
    if (!render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    // ===== Single CommandBuffer for both Scene and UI Passes =====
    auto cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();

    for (auto &system : _materialSystems) {
        system->resetFrameSlot();
    }

    FrameContext ctx;
    {

        // Get primary camera from ECS for runtime/simulation mode
        Entity *runtimeCamera = getPrimaryCamera();
        if (runtimeCamera && runtimeCamera->isValid())
        {

            auto cc = runtimeCamera->getComponent<CameraComponent>();
            auto tc = runtimeCamera->getComponent<TransformComponent>();

            const Extent2D &ext = _viewportRT->getExtent();
            cameraController.update(*tc, *cc, inputManager, ext, dt);
            // Update aspect ratio for runtime camera
            cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
        }


        bool bUseRuntimeCamera = (_appState == AppState::Runtime || _appState == AppState::Simulation) &&
                                 runtimeCamera && runtimeCamera->isValid() &&
                                 runtimeCamera->hasComponent<CameraComponent>();

        if (bUseRuntimeCamera) {
            // Use runtime camera (Entity with CameraComponent)
            auto cc        = runtimeCamera->getComponent<CameraComponent>();
            ctx.view       = cc->getFreeView();
            ctx.projection = cc->getProjection();
        }
        else {
            // Use editor camera (FreeCamera)
            ctx.view       = camera.getViewMatrix();
            ctx.projection = camera.getProjectionMatrix();
        }

        // Extract camera position from view matrix inverse
        glm::mat4 invView = glm::inverse(ctx.view);
        ctx.cameraPos     = glm::vec3(invView[3]);
    }


    // --- MARK: Mirror Rendering (Pre scene render some mirror entities and render to texture for later compositing) ---
    {
        YA_PROFILE_SCOPE("Mirror Pass")
        // Mirror / Rear-view mirror / Screen-in-screen rendering
        // Implementation follows: Render-to-Texture + Render-as-Sprite pattern
        //
        // ┌─────────────────────────────────────────────────────────────────────┐
        // │ Phase 1: Setup                                                      │
        // │   - Get mirror entity position & orientation                        │
        // │   - Calculate mirror view matrix (reflection transform)             │
        // │   - Allocate/fetch mirror RT from RenderTargetPool                  │
        // └─────────────────────────────────────────────────────────────────────┘
        //                              ↓
        // ┌─────────────────────────────────────────────────────────────────────┐
        // │ Phase 2: Render Scene from Mirror's View                            │
        // │   - beginRendering(mirrorRT)                                        │
        // │   - Render scene with mirror camera                                 │
        // │   - endRendering()                                                  │
        // └─────────────────────────────────────────────────────────────────────┘
        //                              ↓
        // ┌─────────────────────────────────────────────────────────────────────┐
        // │ Phase 3: Composite Mirror into Main View                            │
        // │   - Transition mirror RT to ShaderReadOnlyOptimal                   │
        // │   - Render as textured quad in viewport (Render2D::makeSprite)      │
        // └─────────────────────────────────────────────────────────────────────┘

        // TODO: Implement mirror rendering phases
        // 1. Query mirror entities from ECS
        // 2. For each mirror, execute Phase 1-3
        // 3. Consider using RenderTexture helper class for RT management

        auto         scene = getSceneManager()->getActiveScene();
        auto         view  = scene->getRegistry().view<TransformComponent, MirrorComponent>();
        FrameContext ctxCopy;
        bHasMirror = false;
        for (auto [entity, tc, mc] : view.each())
        {
            bHasMirror        = true;
            ctxCopy.viewOwner = entity;
            ctxCopy.cameraPos = tc.getWorldPosition();

            const auto &eye    = tc.getWorldPosition();
            glm::vec3   target = eye + glm::normalize(tc.getForward());
            ctxCopy.view       = FMath::lookAt(eye, target, FMath::Vector::WorldUp);
            ctxCopy.view       = glm::inverse(ctxCopy.view); // Invert view for correct reflection
            break;
        }

        if (bHasMirror) {
            ctxCopy.extent = _mirrorRT->getExtent(); // Ensure material systems render with correct extent for mirror RT

            RenderingInfo ri{
                .label      = "ViewPort",
                .renderArea = Rect2D{
                    .pos    = {0, 0},
                    .extent = _mirrorRT->getExtent().toVec2(), // Use actual RT extent for rendering, which may differ from viewportRect if retro rendering is enabled
                },
                .layerCount       = 1,
                .colorClearValues = {colorClearValue},
                .depthClearValue  = depthClearValue,
                .renderTarget     = _mirrorRT.get(),
            };
            cmdBuf->beginRendering(ri);

            // Render material systems
            for (auto &system : _materialSystems) {
                if (system->bEnabled) {
                    YA_PROFILE_SCOPE(std::format("RenderMaterialSystem_{}", system->_label));
                    system->onRender(cmdBuf.get(), &ctxCopy);
                }
            }
            cmdBuf->endRendering(EndRenderingInfo{
                .renderTarget = _mirrorRT.get(),
            });
        }
    }

    bool bViewPortRectValid = _viewportRect.extent.x > 0 && _viewportRect.extent.y > 0;

    // --- MARK: ViewPort Pass
    if (bViewPortRectValid)
    {
        YA_PROFILE_SCOPE("ViewPort pass")

        // from the editor layer's viewport size
        // auto extent = _editorLayer.g

        auto extent = Extent2D::fromVec2(_viewportRect.extent / _viewportFrameBufferScale);


        _viewportRT->setExtent(extent);

        RenderingInfo ri{
            .label      = "ViewPort",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _viewportRT->getExtent().toVec2(), // Use actual RT extent for rendering, which may differ from viewportRect if retro rendering is enabled
            },
            .layerCount       = 1,
            .colorClearValues = {colorClearValue},
            .depthClearValue  = depthClearValue,
            //
            .renderTarget = _viewportRT.get(),
        };

        cmdBuf->beginRendering(ri);



        ctx.extent = _viewportRT->getExtent(); // Update frame context with actual render extent for material systems
        // Render material systems
        for (auto &system : _materialSystems) {
            if (system->bEnabled) {
                YA_PROFILE_SCOPE(std::format("RenderMaterialSystem_{}", system->_label));
                system->onRender(cmdBuf.get(), &ctx);
            }
        }

        {
            YA_PROFILE_SCOPE("Render2D");
            Render2D::begin(cmdBuf.get());

            if (_appMode == AppMode::Drawing) {
                for (const auto &&[idx, p] : ut::enumerate(clicked))
                {
                    auto tex = idx % 2 == 0
                                 ? AssetManager::get()->getTextureByName("uv1")
                                 : AssetManager::get()->getTextureByName("face");
                    YA_CORE_ASSERT(tex, "Texture not found");
                    glm::vec2 pos;
                    _editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
                    Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
                }

                Render2D::onRender();
                UIManager::get()->render();
                Render2D::onRenderGUI();
                Render2D::end();
            }

            cmdBuf->endRendering(EndRenderingInfo{
                .renderTarget = _viewportRT.get(),
            });
        }
    }


    // --- MARK: Postprocessing
    if (bBasicPostProcessor && bViewPortRectValid)
    {
        YA_PROFILE_SCOPE("Postprocessing pass")
        // 1. Inversion 反向
        // 2. Grayscale 灰度
        auto vkRender = render->as<VulkanRender>();

        vkRender->getDebugUtils()->cmdBeginLabel(cmdBuf->getHandle(), "Postprocessing");
        // Transition postprocess image from Undefined/ShaderReadOnly to ColorAttachmentOptimal
        // Viewport RT is dynamic rendering
        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(),
                                          //   EImageLayout::Undefined,
                                          EImageLayout::ColorAttachmentOptimal);

        // Build RenderingInfo from manual images
        RenderingInfo ri{
            .label      = "Postprocessing",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _viewportRect.extent, // Use viewport size for postprocess render area
            },
            .layerCount       = 1,
            .colorClearValues = {colorClearValue},
            .depthClearValue  = depthClearValue,
            //
            .colorAttachments = {
                RenderingInfo::ImageSpec{
                    .texture     = _postprocessTexture.get(), // ← 使用 App 层的 Texture 接口
                    .sampleCount = ESampleCount::Sample_1,
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                },
            },
        };

        cmdBuf->beginRendering(ri);
        static auto basicPostprocessing = [&]() {
            auto pl = new BasicPostprocessing();
            pl->init();
            _monitorPipelines.push_back({pl->_pipeline->getName(), pl->_pipeline.get()});

            _deleter.push(" basic postprocessing pipeline", [pl](void *) { // Use value capture instead of reference
                delete pl;
            });

            return pl;
        }();

        // Pass input imageView and output extent to render

        const auto &tex = _viewportRT->getCurFrameBuffer()->getColorTexture(0);

        BasicPostprocessing::RenderPayload payload{
            .inputImageView = tex->getImageView(),
            .extent         = Extent2D::fromVec2(_viewportRect.extent),
            .effect         = (BasicPostprocessing::EEffect)_postProcessingEffect,
            .floatParams    = _postProcessingParams,
        };
        basicPostprocessing->render(cmdBuf.get(), payload);
        cmdBuf->endRendering(EndRenderingInfo{});

        // Transition postprocess image to ShaderReadOnlyOptimal for ImGui sampling
        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(),
                                          //   EImageLayout::ColorAttachmentOptimal,
                                          //   EImageLayout::Undefined,
                                          EImageLayout::ShaderReadOnlyOptimal);

        vkRender->getDebugUtils()->cmdEndLabel(cmdBuf->getHandle());

        // Use postprocess texture directly (unified Texture semantics)
        _viewportTexture = _postprocessTexture.get();
    }
    else {
        // Create a Texture wrapper from framebuffer's color attachment for unified semantics
        auto fb          = _viewportRT->getCurFrameBuffer();
        _viewportTexture = fb->getColorTexture(0);
    }

    // Note: _postprocessTexture is now in ShaderReadOnlyOptimal, ready for EditorLayer viewport display

    // --- MARK: 2: Render UI to Swapchain RT ---
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
        auto &imManager = ImGuiManager::get();
        imManager.beginFrame();
        {
            this->renderGUI(dt);
        }
        imManager.endFrame();
        imManager.render();

        if (render->getAPI() == ERenderAPI::Vulkan) {
            imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
        }

        cmdBuf->endRendering(EndRenderingInfo{
            .renderTarget = _screenRT.get(),
        });
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
}

// MARK: Render GUI
void App::onRenderGUI(float dt)
{
    YA_PROFILE_FUNCTION()
    auto &io = ImGui::GetIO();
    if (!ImGui::Begin("App Info"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Render 2D", 0)) {
        ImGui::Indent();
        // temp code here to adopt to new Render2D
        Render2D::onImGui();
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Render Target Pool", 0)) {
        ImGui::Indent();
        // RenderTargetPool::get().onRenderGUI();
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Material Systems", 0)) {
        ImGui::Indent();
        for (auto &system : _materialSystems) {
            system->renderGUI();
        }
        ImGui::Unindent();
    }


    _viewportRT->onRenderGUI();
    _screenRT->onRenderGUI();

    if (ImGui::CollapsingHeader("Context", ImGuiTreeNodeFlags_DefaultOpen)) {
        float fps = 1.0f / dt;
        ImGui::Text("%s", std::format("Frame: {}, DeltaTime: {:.2f} ms,\t FPS: {:.1f}", _frameIndex, dt * 1000.0f, fps).data());
        static int count = 0;
        if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
            count++;
            YA_CORE_INFO("=====================================");
        }

        if (ImGui::TreeNode("ImGUI")) {
            ImGuiManager::get().onRenderGUI();
            ImGui::TreePop();
        }

        ImGui::DragFloat("Retro Scale", &_viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);

        auto *swapchain = _render->getSwapchain();
        bool  bVsync    = swapchain->getVsync();
        if (ImGui::Checkbox("VSync", &bVsync)) {
            taskManager.registerFrameTask([swapchain, bVsync]() {
                swapchain->setVsync(bVsync);
            });
        }

        EPresentMode::T presentMode  = swapchain->getPresentMode();
        const char     *presentModes = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
        if (ImGui::Combo("Present Mode", reinterpret_cast<int *>(&presentMode), presentModes)) {
            taskManager.registerFrameTask([swapchain, presentMode]() {
                swapchain->setPresentMode(presentMode);
            });
        }

        AppMode mode = _appMode;
        if (ImGui::Combo("App Mode", reinterpret_cast<int *>(&mode), "Control\0Drawing\0")) {
            _appMode = mode;
        }

        std::string clickedPoints;
        for (const auto &p : clicked) {
            clickedPoints += std::format("({}, {}) ", (int)p.x, (int)p.y);
        }
        ImGui::Text("Clicked Points: %s", clickedPoints.c_str());

        if (ImGui::Button("Deserialize Scene"))
        {
            auto sceneManager = ya::App::get()->getSceneManager();
            YA_CORE_ASSERT(sceneManager, "SceneManager is null");
            sceneManager->serializeToFile("Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json",
                                          getSceneManager()->getActiveScene());
        }
        if (ImGui::TreeNode("Monitor Pipelines")) {
            for (const auto &[name, pipeline] : _monitorPipelines) {
                ImGui::Text("%s", name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("reload")) {
                    pipeline->reloadShaders();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Postprocessing")) {

            ImGui::Checkbox("Basic Postprocessing", &bBasicPostProcessor);
            if (!bBasicPostProcessor) {
                ImGui::BeginDisabled();
            }
            {
                ImGui::Combo("Effect",
                             reinterpret_cast<int *>(&_postProcessingEffect),
                             "Inversion\0Grayscale\0Weighted Grayscale\0"
                             "Kernel_Sharpe\0Kernel_Blur\0Kernel_Edge-Detection\0Tone Mapping\0"
                             // frag shader do nothing, 老电视机花屏效果
                             "Random\0");
                for (const auto &[i, p] : ut::enumerate(_postProcessingParams)) {
                    ImGui::DragFloat4(std::format("{}", i).c_str(), &p.x);
                }
            }
            if (!bBasicPostProcessor) {
                ImGui::EndDisabled();
            }

            ImGui::TreePop();
        }
    }

    imcEditorCamera(camera);
    imcClearValues();
    imcFpsControl(*FPSControl::get());

    static bool bDarkMode = true;
    if (ImGui::Checkbox("Dark Mode", &bDarkMode)) {
        if (bDarkMode) {
            ImGui::StyleColorsDark();
        }
        else {
            ImGui::StyleColorsLight();
        }
    }

    ImGui::End();
}


// MARK: Scene
bool App::loadScene(const std::string &path)
{
    switch (_appState) {
    case AppState::Runtime:
    case AppState::Simulation:
        stopRuntime();
        break;
    case AppState::Editor:
        break;
    }

    if (_sceneManager) {
        return _sceneManager->loadScene(path);
    }
    return false;
}

bool App::unloadScene()
{
    if (_sceneManager) {
        return _sceneManager->unloadScene();
    }
    return false;
}

// void App::onSceneInit(Scene *scene)
// {

//     // Create camera entity
// }

void App::onSceneDestroy(Scene *scene)
{
    // No longer need to clear runtime camera reference
    // as we query it from ECS each frame
}

void App::onSceneActivated(Scene *scene)
{
    _editorLayer->setSceneContext(scene);

    // Engine core initialization - basic scene setup
    // Application-specific logic should be in derived classes (e.g., HelloMaterial)
}

// State transition hooks
void App::onEnterRuntime()
{
}

void App::startRuntime()
{
    if (_appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start runtime: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting runtime...");
    _sceneManager->onStartRuntime();
    _appState = AppState::Runtime;


    onEnterRuntime();
}

void App::startSimulation()
{
    if (_appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start simulation: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting simulation...");
    _appState = AppState::Simulation;

    onEnterSimulation();
}

void App::stopRuntime()
{
    if (_appState != AppState::Runtime) {
        YA_CORE_WARN("Cannot stop: not in runtime mode");
        return;
    }

    YA_CORE_INFO("Stopping runtime");
    _appState = AppState::Editor;
    _sceneManager->onStopRuntime();
    _luaScriptingSystem->onStop();
}

void App::stopSimulation()
{
    if (_appState != AppState::Simulation) {
        YA_CORE_WARN("Cannot stop: not in simulation mode");
        return;
    }

    YA_CORE_INFO("Stopping simulation");
    _appState = AppState::Editor;

    onExitSimulation();
}

bool App::onWindowResized(const WindowResizeEvent &event)
{
    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", w, h, aspectRatio);
    // camera.setAspectRatio(aspectRatio);
    _windowSize = {w, h};

    // Notify RenderTargetPool of window resize
    // RenderTargetPool::get().onWindowResized(static_cast<uint32_t>(w), static_cast<uint32_t>(h));

    return false;
}

bool App::onKeyReleased(const KeyReleasedEvent &event)
{
    if (event.getKeyCode() == EKey::Escape) {
        YA_CORE_INFO("{}", event.toString());
        requestQuit();
        return true;
    }
    return false;
}

bool App::onMouseMoved(const MouseMoveEvent &event)
{
    _lastMousePos = glm::vec2(event.getX(), event.getY());
    return false;
}

bool App::onMouseButtonReleased(const MouseButtonReleasedEvent &event)
{
    // YA_CORE_INFO("Mouse Button Released: {}", event.toString());
    switch (_appMode) {
    case Control:
        break;
    case Drawing:
    {
        // TODO: make a cmdList to async render and draw before Render2D::begin or after Render2D::end
        if (event.GetMouseButton() == EMouse::Left) {
            clicked.push_back(_lastMousePos);
        }
    } break;
    }

    return false;
}

bool App::onMouseScrolled(const MouseScrolledEvent &event)
{
    return false;
}

Entity *App::getPrimaryCamera() const
{
    if (!_sceneManager) {
        return nullptr;
    }

    Scene *scene = _sceneManager->getActiveScene();
    if (!scene || !scene->isValid()) {
        return nullptr;
    }

    auto &registry = scene->getRegistry();

    // Strategy 1: Find camera with PlayerComponent (fallback)
    auto playerCameraView = registry.view<CameraComponent, PlayerComponent>();
    for (auto entity : playerCameraView) {
        return scene->getEntityByEnttID(entity);
    }

    // Strategy 2: Find camera with _primary == true
    auto view = registry.view<CameraComponent>();
    for (auto entity : view) {
        auto &cc = view.get<CameraComponent>(entity);
        if (cc.bPrimary) {
            return scene->getEntityByEnttID(entity);
        }
    }

    // No primary camera found
    return nullptr;
}



} // namespace ya
