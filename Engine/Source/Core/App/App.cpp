#include "App.h"


// Core
#include "Core/App/App.h"
#include "Core/App/FPSCtrl.h"
#include "Core/App/SDLMisc.h"
#include "Core/AssetManager.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"
#include "Core/System/FileSystem.h"
#include "Core/System/FileWatcher.h"



// Managers/System
#include "Core/Manager/Facade.h"
#include "ImGuiHelper.h"
#include "Render/Render.h"
#include "Render/TextureLibrary.h"
#include "Scene/SceneManager.h"



// ECS
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/LitMaterialSystem.h"
#include "ECS/System/LuaScriptingSystem.h"
#include "ECS/System/SimpleMaterialSystem.h"
#include "ECS/System/UnlitMaterialSystem.h"



// Render
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"



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

// Windows
#if defined(_WIN32)
    #include <windows.h>
#endif

// Scene
#include "Scene/Scene.h"



std::vector<glm::vec2> clicked;

#define ONLY_2D 0


namespace ya
{

// Define the static member variable
App     *App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;


void App::onSceneViewportResized(Rect2D rect)
{
    viewportRect      = rect;
    float aspectRatio = rect.extent.x > 0 && rect.extent.y > 0 ? rect.extent.x / rect.extent.y : 16.0f / 9.0f;
    camera.setAspectRatio(aspectRatio);
    _viewportRT->setExtent(Extent2D{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    });
}

// ===== TODO: These global variables should be moved to appropriate managers =====

const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;



std::shared_ptr<Mesh> cubeMesh;



ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

#pragma region misc


void imcFpsControl(FPSControl &fpsCtrl)
{
    if (ImGui::CollapsingHeader("FPS Control", 0)) {
        ImGui::Indent();

        ImGui::Text("FPS Limit: %.1f", fpsCtrl.fpsLimit);

        static float newFpsLimit = fpsCtrl.fpsLimit;
        ImGui::PushItemWidth(100.0f);
        ImGui::InputFloat("New: ", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Confirm")) {
            fpsCtrl.setFPSLimit(newFpsLimit);
        }

        ImGui::Checkbox("Enable FPS Control", &fpsCtrl.bEnable);
        ImGui::Unindent();
    }
}

bool imcEditorCamera(FreeCamera &camera)
{
    auto position = camera._position;
    auto rotation = camera._rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls", 0)) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &App::get()->cameraController._moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &App::get()->cameraController._rotationSpeed, 1.f, 10.f, 180.f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }


    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}

void imcClearValues()
{
    if (ImGui::CollapsingHeader("Clear Values", 0)) {
        float color[4] = {colorClearValue.color.r, colorClearValue.color.g, colorClearValue.color.b, colorClearValue.color.a};
        if (ImGui::ColorEdit4("Color Clear Value", color)) {
            colorClearValue = ClearValue(color[0], color[1], color[2], color[3]);
        }
        float depth = depthClearValue.depthStencil.depth;
        if (ImGui::DragFloat("Depth Clear Value", &depth, 0.01f, 0.0f, 1.0f)) {
            depthClearValue = ClearValue(depth, depthClearValue.depthStencil.stencil);
        }
    }
}

#pragma endregion

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
        FileSystem::init();
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

    _viewportRenderPass = IRenderPass::create(_render);
    _viewportRenderPass->recreate(RenderPassCreateInfo{
        .label       = "Viewport RenderPass",
        .attachments = {
            // Color attachment (will be sampled by ImGui)
            AttachmentDescription{
                .index          = 0,
                .format         = EFormat::R8G8B8A8_UNORM,
                .samples        = _sampleCount,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal, // For ImGui sampling
                .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            // Depth attachment
            AttachmentDescription{
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
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex     = 0,
                .inputAttachments = {},
                .colorAttachments = {
                    RenderPassCreateInfo::AttachmentRef{
                        .ref    = 0,
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depthAttachment = RenderPassCreateInfo::AttachmentRef{
                    .ref    = 1,
                    .layout = EImageLayout::DepthStencilAttachmentOptimal,
                },
                .resolveAttachment = {},
            },
        },
        .dependencies = {
            RenderPassCreateInfo::SubpassDependency{
                .bSrcExternal = true,
                .srcSubpass   = 0,
                .dstSubpass   = 0,
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
                _screenRT->setExtent(newExtent);
            }
            if ((now.extent.width != old.extent.width ||
                 now.extent.height != old.extent.height ||
                 old.presentMode != now.presentMode))
            {
                _screenRT->setExtent(newExtent);
            }
        });

    _renderpass = IRenderPass::create(_render);
    _renderpass->recreate(RenderPassCreateInfo{
        .label       = "Final RenderPass",
        .attachments = {
            // color to present (swapchain)
            AttachmentDescription{
                .index          = 0,
                .format         = EFormat::R8G8B8A8_UNORM, // TODO: detect by device
                .samples        = ESampleCount::Sample_1,  // first present attachment cannot be multi-sampled
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::PresentSrcKHR,
                .usage          = EImageUsage::ColorAttachment,
            },
        },
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex     = 0,
                .inputAttachments = {},
                .colorAttachments = {
                    RenderPassCreateInfo::AttachmentRef{
                        .ref    = 0, // color attachment
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depthAttachment   = {},
                .resolveAttachment = {},
            },
        },
        .dependencies = {
            RenderPassCreateInfo::SubpassDependency{
                .bSrcExternal = true,
                .srcSubpass   = 0,
                .dstSubpass   = 0,
            },
        },
    });

    // Create Scene RenderTarget (offscreen for 3D scene)
    _viewportRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .bSwapChainTarget = false,
        .renderPass       = _viewportRenderPass.get(),
        .frameBufferCount = 1,
        .extent           = glm::vec2(static_cast<float>(winW), static_cast<float>(winH)),
    });
#if !ONLY_2D
    _viewportRT->addMaterialSystem<SimpleMaterialSystem>();
    _viewportRT->addMaterialSystem<UnlitMaterialSystem>();
    _viewportRT->addMaterialSystem<LitMaterialSystem>();
#endif

    // Create UI RenderTarget (swapchain for ImGui)
    _screenRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Final RenderTarget",
        .bSwapChainTarget = true,
        .renderPass       = _renderpass.get(),
    });

#pragma region ImGui Init
    // Initialize ImGui Manager
    auto &imManager = ImGuiManager::get();
    imManager.init(_render, _renderpass.get());
#pragma endregion



    {
        YA_PROFILE_SCOPE_LOG("Inheritance Init");
        onInit(ci);
    }


    // ===== Initialize TextureLibrary =====
    TextureLibrary::init();

    // ===== Initialize SceneManager =====
    _sceneManager = new SceneManager();
    // _sceneManager->onSceneInit.addLambda(this, [this](Scene *scene) { this->onSceneInit(scene); });
    _sceneManager->onSceneDestroy.addLambda(this, [this](Scene *scene) { this->onSceneDestroy(scene); });
    _sceneManager->onSceneActivated.addLambda(this, [this](Scene *scene) { this->onSceneActivated(scene); });

    // FIXME: current 2D rely on the the white texture of App, fix dependencies and move before load scene
    // Initialize Render2D with Scene RenderPass (has depth attachment) for compatibility with both passes
    // The pipeline's depthTestEnable=false allows it to work in UI pass without depth
    Render2D::init(_render, _viewportRenderPass.get());
    // wait something done
    _render->waitIdle();

    {
        YA_PROFILE_SCOPE_LOG("Post Init");
        onPostInit();
    }

    _editorLayer = new EditorLayer(this);
    _editorLayer->onAttach();
    // todo: use ref
    _editorLayer->onViewportResized.set([this](Rect2D rect) {
        onSceneViewportResized(rect);
    });

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

void App::renderGUI(float dt)
{
    _editorLayer->onImGuiRender([this, dt]() {
        this->onRenderGUI(dt);
    });
}

// MARK: INIT
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

    bool bInViewport = FUIHelper::isPointInRect(_lastMousePos, viewportRect.pos, viewportRect.extent);
    // currently ui only rendering in viewport
    if (bInViewport) {


        UIAppCtx ctx{
            .lastMousePos = _lastMousePos,
            .bInViewport  = bInViewport,
            .viewportRect = viewportRect,
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

    // Cleanup render targets before render passes (dependency order)
    if (_viewportRT) {
        _viewportRT->destroy();
        _viewportRT.reset();
    }

    if (_screenRT) {
        _screenRT->destroy();
        _screenRT.reset();
    }

    _renderpass.reset();
    _viewportRenderPass.reset();

    TextureLibrary::destroy();
    FontManager::get()->cleanup();
    AssetManager::get()->cleanup();

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
        onUpdate(dt);
    }
    onRender(dt);
    taskManager.update();
    ++_frameIndex;
    return 0;
}


void App::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION()
    inputManager.preUpdate();
    Facade.timerManager.onUpdate(dt);

    // 文件监视器轮询（检测文件变化）
    if (auto *watcher = FileWatcher::get()) {
        watcher->poll();
    }

    // Store real-time delta for editor

    _viewportRT->setColorClearValue(colorClearValue);
    _viewportRT->setDepthStencilClearValue(depthClearValue);

    _viewportRT->onUpdate(dt);
    _screenRT->onUpdate(dt);

    auto cam = _viewportRT->getCameraMut();

    bool bShouldUpdateEntityCamera = _editorLayer->isViewportHovered() ||
                                     _editorLayer->isViewportFocused();

    if (bShouldUpdateEntityCamera) {
        cameraController.update(camera, inputManager, dt); // Camera expects dt in seconds
        if (cam && cam->isValid() && cam->hasComponent<CameraComponent>()) {
            auto            cc  = cam->getComponent<CameraComponent>();
            const Extent2D &ext = _viewportRT->getExtent();
            if (cam->hasComponent<TransformComponent>()) {
                auto tc = cam->getComponent<TransformComponent>();
                orbitCameraController.update(*tc, *cc, inputManager, ext, dt);
            }
        }
    }

    switch (_appState) {

    case AppState::Editor:
        break;
    case AppState::Simulation:
    case AppState::Runtime:
    {
        _luaScriptingSystem->onUpdate(dt);
    } break;
    }


    _editorLayer->onUpdate(dt);
    inputManager.postUpdate();
}

// MARK: Render
void App::onRender(float dt)
{
    YA_PROFILE_FUNCTION()
    auto render = getRender();

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
    {
        cmdBuf->reset();
        cmdBuf->begin();

        // --- MARK: PASS 1: Render 3D Scene to Offscreen RT ---
        {
            YA_PROFILE_SCOPE("ViewPort pass")
            _viewportRT->begin(cmdBuf.get());
            _viewportRT->onRender(cmdBuf.get());

            {
                YA_PROFILE_SCOPE("Render2D");
                // Render 2D overlays on scene (pass frame index for per-frame DescriptorSets)
                Render2D::begin(cmdBuf.get());
                static glm::vec3 pos1 = glm::vec3(0.f, 0, 0);

                if (_appMode == AppMode::Drawing) {
                    for (const auto &&[idx, p] : ut::enumerate(clicked))
                    {
                        auto tex = idx % 2 == 0
                                     ? AssetManager::get()->getTextureByName("uv1")
                                     : AssetManager::get()->getTextureByName("face");
                        YA_CORE_ASSERT(tex, "Texture not found");
                        glm::vec3 pos = glm::vec3(p.x - viewportRect.pos.x, p.y - viewportRect.pos.y, 0.0f);
                        Render2D::makeSprite(pos, {50, 50}, tex);
                    }
                }

                // auto font = FontManager::get()->getFont("JetBrainsMono-Medium", 48);
                // Render2D::makeText("Hello YaEngine!", pos1 + glm::vec3(200.0f, 200.0f, -0.1f), FUIColor::red().asVec4(), font.get());

                UIManager::get()->render();
                Render2D::onRenderGUI();
                Render2D::end();
            }

            _viewportRT->end(cmdBuf.get());
        }

        {
            YA_PROFILE_SCOPE("Screen pass")
            // --- MARK: PASS 2: Render UI to Swapchain RT ---
            _screenRT->begin(cmdBuf.get());

            // Render ImGui
            auto &imManager = ImGuiManager::get();
            imManager.beginFrame();

            this->renderGUI(dt);

            // imcDrawMaterials();
            imManager.endFrame();
            imManager.render();

            if (render->getAPI() == ERenderAPI::Vulkan) {
                imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
            }

            _screenRT->end(cmdBuf.get());
        }
    }

    cmdBuf->end();

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
    // Clear camera references in render targets to avoid dangling Entity pointers
    // when Scene is destroyed
    // if (_viewportRT) {
    //     auto cam = _viewportRT->getCameraMut();
    //     if (cam && cam->getScene() == scene) {
    //         _viewportRT->setCamera(nullptr);
    //     }
    // }

    // if (_screenRT) {
    //     auto cam = _screenRT->getCameraMut();
    //     if (cam && cam->getScene() == scene) {
    //         _screenRT->setCamera(nullptr);
    //     }
    // }
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

void App::imcDrawMaterials()
{
}

void App::handleSystemSignals()
{
#if !defined(_WIN32)
    auto handler = [](int signal) {
        if (!App::_instance) {
            return;
        }
        YA_CORE_INFO("Received signal: {}", signal);

        switch (signal) {
        case SIGINT:
        case SIGTERM:
        {
            App::_instance->requestQuit();
        } break;
        default:
            break;
        }
    };

    std::signal(SIGINT, handler);  // Ctrl+C
    std::signal(SIGTERM, handler); // Termination request
#else
    // linux: will continue to execute after handle the signal
    // Windows: need 用SetConsoleCtrlHandler来拦截并阻止默认退出
    SetConsoleCtrlHandler(
        [](DWORD dwCtrlType) -> BOOL {
            switch (dwCtrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
                YA_CORE_INFO("Received Ctrl+C, requesting graceful shutdown...");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true; // 返回TRUE阻止默认的终止行为
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                YA_CORE_INFO("Received system shutdown event");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true; // 返回TRUE阻止默认的终止行为
            };

            return FALSE; // 对于其他事件，使用默认处理
        },
        TRUE);
#endif
}



} // namespace ya
