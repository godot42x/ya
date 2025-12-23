#include "App.h"


// Core
#include "Core/App/FPSCtrl.h"
#include "Core/AssetManager.h"
#include "Core/Camera.h"
#include "Core/Event.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"

// Managers/System
#include "Core/Subsystem/ReflectionSystem.h"
#include "ImGuiHelper.h"
#include "Render/Material/LitMaterial.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Render.h"
#include "Render/TextureLibrary.h"
#include "Scene/SceneManager.h"



// ECS
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/LitMaterialSystem.h"
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
App *App::_instance = nullptr;

// Implementation of getter methods
Scene *App::getScene() const
{
    return _sceneManager ? _sceneManager->getCurrentScene() : nullptr;
}

// ===== TODO: These global variables should be moved to appropriate managers =====

const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;



ImguiState imgui;

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
    YA_PROFILE_FUNCTION();
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    // register terminal  C-c signal
    handleSystemSignals();


    {
        YA_PROFILE_SCOPE("App Init Subsystems");
        Logger::init();
        FileSystem::init();
        MaterialFactory::init();
        ReflectionSystem::init();
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
        .shaderName = "Test/Lit.glsl",
    });


    // ===== Initialize Render =====
    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .presentMode   = EPresentMode::FIFO, // vsync
            .bVsync        = true,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

    // Get window size
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);
    _windowSize.x = static_cast<float>(winW);
    _windowSize.y = static_cast<float>(winH);

    // Allocate command buffers
    _render->allocateCommandBuffers(_render->getSwapchainImageCount(), _commandBuffers);

    constexpr auto _sampleCount = ESampleCount::Sample_1; // TODO: support MSAA
    // MARK: RenderPass

    /**
      In Vulkan:
        Create render pass and subpass
        define all attachments,
        input/color/depth/resolved attachment ref from all attachments
        and each subpasses dependencies (source -> next)
     */
    _renderpass = IRenderPass::create(_render);
    _renderpass->recreate(RenderPassCreateInfo{
        .attachments = {
            // color to present
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
            // depth attachment
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
                        .ref    = 0, // color attachment
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depthAttachment = RenderPassCreateInfo::AttachmentRef{
                    .ref    = 1, // depth attachment
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

    // Create main render target
    _rt = ya::createRenderTarget(_renderpass.get());
#if !ONLY_2D
    _rt->addMaterialSystem<SimpleMaterialSystem>();
    _rt->addMaterialSystem<UnlitMaterialSystem>();
    _rt->addMaterialSystem<LitMaterialSystem>();
#endif

#pragma region ImGui Init
    // Initialize ImGui Manager
    _imguiManager = new ImGuiManager();
    _imguiManager->init(_render, _renderpass.get());
    imgui = *_imguiManager; // Legacy compatibility
#pragma endregion



    {
        YA_PROFILE_SCOPE("Inheritance Init");
        onInit(ci);
    }


    // ===== Initialize TextureLibrary =====
    TextureLibrary::init();

    // ===== Initialize SceneManager =====
    _sceneManager = new SceneManager();
    _sceneManager->setSceneInitCallback([this](Scene *scene) {
        this->onSceneInit(scene);
    });
    _sceneManager->setSceneCleanupCallback([this](Scene *scene) {
        this->onSceneDestroy(scene);
    });


    // FIXME: current 2D rely on the the white texture of App, fix dependencies and move before load scene
    Render2D::init(_render, _renderpass.get());
    // wait something done
    _render->waitIdle();

    {
        YA_PROFILE_SCOPE("Post Init");
        onPostInit();
    }

    loadScene(ci.defaultScenePath);
}

// MARK: INIT
void App::onInit(AppDesc ci)
{
    auto &bus = *MessageBus::get();
    bus.subscribe<WindowResizeEvent>(this, &App::onWindowResized);
    bus.subscribe<KeyReleasedEvent>(this, &App::onKeyReleased);
    bus.subscribe<MouseScrolledEvent>(this, &App::onMouseScrolled);


    FontManager::get()->loadFont("Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 48);

    auto panel     = UIFactory::create<UIPanel>();
    panel->_color  = FUIColor(0.2f, 0.2f, 0.2f, 0.8f);
    auto btn       = UIFactory::create<UIButton>();
    btn->_position = {50.0f, 50.0f};
    btn->_size     = {200.0f, 100.0f};
    panel->addChild(btn);

    auto textBlock   = UIFactory::create<UITextBlock>();
    textBlock->_font = FontManager::get()->getFont("JetBrainsMono-Medium", 48).get();
    btn->addChild(textBlock);


    auto mgr = UIManager::get();
    mgr->_rootCanvas->addChild(panel);
}

void App::onPostInit()
{
    // those resources are depends on the render context
    // LOAD demo testing textures
    const char *faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char *uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTexture("face", faceTexturePath);
    ya::AssetManager::get()->loadTexture("uv1", uv1TexturePath);
}

int App::onEvent(const Event &event)
{
    switch (event.getEventType()) {
    case EEvent::MouseMoved:
    {
        onMouseMoved(static_cast<const MouseMoveEvent &>(event));

    } break;
    case EEvent::MouseButtonReleased:
    {
        onMouseButtonReleased(static_cast<const MouseButtonReleasedEvent &>(event));
    } break;
    default:
        break;
    }


    UIAppCtx ctx{
        .lastMousePos = _lastMousePos,
    };
    UIManager::get()->onEvent(event, ctx);
    return 0;
}


// MARK: QUIT
void ya::App::quit()
{
    if (_render) {
        _render->waitIdle();
    }
    {
        YA_PROFILE_SCOPE("Inheritance Quit");
        onQuit();
    }

    unloadScene();
    MaterialFactory::get()->destroy();

    Render2D::destroy();

    // Cleanup managers
    if (_imguiManager) {
        _imguiManager->shutdown();
        delete _imguiManager;
        _imguiManager = nullptr;
    }

    // TODO: manage by render or render-context
    // Cleanup render target before render pass (dependency order)
    if (_rt) {
        _rt->destroy();
        _rt.reset();
    }

    _renderpass.reset(); // shared_ptr, use reset() instead of delete

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

    if (_sceneManager) {
        delete _sceneManager;
        _sceneManager = nullptr;
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
    // TODO: refactor event system
    //  use bus to dispatch event to each system or subsystems is great
    // but  there is not serial order for processing events
    // for example: imgui want to capture mouse/keyboard event first
    // if subscribe it in imgui's obj, it may be after other systems

    // YA_CORE_TRACE("Event processed: {}", event.type);
    EventProcessState ret = imgui.processEvents(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }
    inputManager.processEvent(event);

#pragma region Sdl Event

    switch (SDL_EventType(event.type))
    {
    case SDL_EVENT_FIRST:
        break;
    case SDL_EVENT_QUIT:
    {
        requestQuit();
        return 1;
    } break;
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_LOCALE_CHANGED:
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MOVED:
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    {
        dispatchEvent(WindowResizeEvent(event.window.windowID, event.window.data1, event.window.data2));
    } break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    {
        YA_CORE_INFO("Window minimized");
        _bMinimized = true;
    } break;
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    {
        YA_CORE_INFO("Window restored/maximized");
        _bMinimized = false;
    } break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    {
        dispatchEvent(WindowFocusEvent(event.window.windowID));
    } break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    {
        dispatchEvent(WindowFocusLostEvent(event.window.windowID));
    } break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        requestQuit();
        return 1;
    } break;
    case SDL_EVENT_WINDOW_HIT_TEST:
    case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_DESTROYED:
    case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
    case SDL_EVENT_KEY_DOWN:
    {
        KeyPressedEvent ev;
        ev._keyCode = (enum EKey::T)event.key.key;
        ev._mod     = event.key.mod;
        ev.bRepeat  = event.key.repeat; // SDL3中的repeat字段
        dispatchEvent(ev);
    } break;
    case SDL_EVENT_KEY_UP:
    {
        KeyReleasedEvent ev;
        ev._keyCode = static_cast<EKey::T>(event.key.key);
        ev._mod     = event.key.mod;
        dispatchEvent(ev);
    } break;
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_KEYMAP_CHANGED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION:
    {
        MouseMoveEvent ev(event.motion.x, event.motion.y);
        // Global size from the window top-left
        // YA_CORE_INFO("Mouse Move: {}, {}", event.motion.x, event.motion.y);
        dispatchEvent(ev);
    } break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        MouseButtonPressedEvent ev(event.button.button);
        dispatchEvent(ev);

    } break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        MouseButtonReleasedEvent ev(event.button.button);
        dispatchEvent(ev);

    } break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        MouseScrolledEvent ev;
        ev._offsetX = event.wheel.x;
        ev._offsetY = event.wheel.y;
        dispatchEvent(ev);
    } break;
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BALL_MOTION:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
    case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_CLIPBOARD_UPDATE:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
    case SDL_EVENT_SENSOR_UPDATE:
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_AXIS:
    case SDL_EVENT_CAMERA_DEVICE_ADDED:
    case SDL_EVENT_CAMERA_DEVICE_REMOVED:
    case SDL_EVENT_CAMERA_DEVICE_APPROVED:
    case SDL_EVENT_CAMERA_DEVICE_DENIED:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_DEVICE_LOST:
    case SDL_EVENT_PRIVATE0:
    case SDL_EVENT_PRIVATE1:
    case SDL_EVENT_PRIVATE2:
    case SDL_EVENT_PRIVATE3:
    case SDL_EVENT_POLL_SENTINEL:
    case SDL_EVENT_USER:
    case SDL_EVENT_LAST:
    case SDL_EVENT_ENUM_PADDING:
        break;
    }
#pragma endregion


    return 0;
};


int ya::App::iterate(float dt)
{
    SDL_Event evt;
    SDL_PollEvent(&evt);

    if (auto result = processEvent(evt); result != 0) {
        return 1;
    }

    dt += FPSControl::get()->update(dt);

    // Skip rendering when minimized to avoid swapchain recreation with invalid extent
    // TODO: only skip render, but still update logic
    if (_bMinimized) {
        SDL_Delay(200); // Small delay to reduce CPU usage when minimized
        YA_CORE_INFO("Application minimized, skipping frame");
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
    inputManager.update();

    _rt->setColorClearValue(colorClearValue);
    _rt->setDepthStencilClearValue(depthClearValue);
    _rt->onUpdate(dt);

    auto cam = _rt->getCameraMut();

    cameraController.update(camera, inputManager, dt); // Camera expects dt in seconds
    if (cam && cam->hasComponent<CameraComponent>()) {
        auto            cc  = cam->getComponent<CameraComponent>();
        const Extent2D &ext = _rt->getExtent();
        if (cam->hasComponent<TransformComponent>()) {
            auto tc = cam->getComponent<TransformComponent>();
            orbitCameraController.update(*tc, *cc, inputManager, ext);
        }
    }

    // auto render = getRender();
    // auto cmdBuf = render->beginIsolateCommands();
    // render->endIsolateCommands(cmdBuf);
}

// MARK: Render
void App::onRender(float dt)
{
    auto render = getRender();

    if (_windowSize.x <= 0 || _windowSize.y <= 0) {
        YA_CORE_INFO("{}x{}: Window minimized, skipping frame", _windowSize.x, _windowSize.y);
        return;
    }

    int32_t imageIndex = -1;
    if (!render->begin(&imageIndex)) {
        return;
    }

    // Skip rendering if imageIndex is invalid (e.g., window minimized during swapchain recreation)
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    // 2. begin command buffer
    auto curCmdBuf = _commandBuffers[imageIndex];
    curCmdBuf->reset();
    curCmdBuf->begin();

    // Get native command buffer handle for render operations
    CommandBufferHandle cmdBufHandle = curCmdBuf->getHandle();

    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    _rt->begin(curCmdBuf.get());
    _rt->onRender(curCmdBuf.get());
    // _rt->getRenderPass()->beginDynamicRendering();

    static glm::vec3 pos1 = glm::vec3(0.f, 0, 0);
    static glm::vec3 pos2 = glm::vec3(100.f, 100.f, 0.f);
    static glm::vec3 pos3 = glm::vec3(500.f, 500.f, 0.f);
    static glm::vec3 pos4 = glm::vec3(-500.f, -500.f, 0.f);

    // MARK: Render2D
    Render2D::begin(curCmdBuf.get());
    if (_appMode == AppMode::Drawing) {
        for (const auto &&[idx, p] : ut::enumerate(clicked))
        {
            auto tex = idx % 2 == 0
                         ? AssetManager::get()->getTextureByName("uv1")
                         : AssetManager::get()->getTextureByName("face");
            YA_CORE_ASSERT(tex, "Texture not found");

            Render2D::makeSprite({p.x, p.y, 0}, {50, 50}, tex);
        }
    }
    auto font = FontManager::get()->getFont("JetBrainsMono-Medium", 48);
    Render2D::makeText("Hello YaEngine!", pos1 + glm::vec3(200.0f, 200.0f, -0.1f), FUIColor::red().asVec4(), font.get());
    UIManager::get()->render();
    Render2D::end();


#pragma region ImGui
    imgui.beginFrame();
    {
        if (ImGui::CollapsingHeader("Render 2D", 0)) {
            ImGui::Indent();
            ImGui::DragFloat3("pos1", glm::value_ptr(pos1), 0.1f);
            ImGui::DragFloat3("pos2", glm::value_ptr(pos2), 1.f);
            ImGui::DragFloat3("pos3", glm::value_ptr(pos3), 1.f);
            ImGui::DragFloat3("pos4", glm::value_ptr(pos4), 1.f);
            Render2D::onRenderGUI();
            ImGui::Unindent();
        }


        if (ImGui::CollapsingHeader("Context", ImGuiTreeNodeFlags_DefaultOpen)) {
            float fps = 1.0f / dt;
            ImGui::Text("%s", std::format("Frame: {}, DeltaTime: {:.2f} ms,\t FPS: {:.1f}", _frameIndex, dt * 1000.0f, fps).data());
            static int count = 0;
            if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
                count++;
                YA_CORE_INFO("=====================================");
            }

            // ImGui::DragFloat("Texture Mix Alpha", &pushData.textureMixAlpha, 0.01, 0, 1);

            auto *swapchain = render->getSwapchain();
            bool  bVsync    = swapchain->getVsync();
            if (ImGui::Checkbox("VSync", &bVsync)) {
                taskManager.registerFrameTask([swapchain, bVsync]() {
                    // TODO :bind dirty link
                    swapchain->setVsync(bVsync);
                });
            }


            EPresentMode::T presentMode  = swapchain->getPresentMode();
            const char     *presentModes = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
            if (ImGui::Combo("Present Mode", reinterpret_cast<int *>(&presentMode), presentModes)) {
                taskManager.registerFrameTask([swapchain, presentMode]() {
                    // TODO :bind dirty link
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
                sceneManager->serializeToFile("Example/HelloMaterial/Content/Scenes/HelloMaterialScene.json",
                                              getSceneManager()->getCurrentScene());
            }
        }
        onRenderGUI();


        imcEditorCamera(camera);
        imcClearValues();
        imcFpsControl(*FPSControl::get());
        // static bool bItemPicker = false;
        // ImGui::Checkbox("Debug Picker", &bItemPicker);
        // if (bItemPicker) {
        //     ImGui::DebugStartItemPicker();
        // }
        static bool bDarkMode = true;
        if (ImGui::Checkbox("Dark Mode", &bDarkMode)) {
            if (bDarkMode) {
                ImGui::StyleColorsDark();
            }
            else {
                ImGui::StyleColorsLight();
            }
        }
        auto *scene = getScene();
        if (scene) {
            if (auto *random = scene->getEntityByID(10); random && random->hasComponent<TransformComponent>()) {
                auto tc = random->getComponent<TransformComponent>();

                if (ImGui::CollapsingHeader("Random Cube Transform", 0)) {
                    ImGui::DragFloat3("Position", glm::value_ptr(tc->_position), 0.1f);
                    ImGui::DragFloat3("Rotation", glm::value_ptr(tc->_rotation), 1.f);
                    // TODO: Fix this - glm::value_ptr issue
                    // ImGui::DragFloat3("Scale", glm::value_ptr(tc->_scale), 0.1f, 0.1f);
                    float scale[3] = {tc->_scale.x, tc->_scale.y, tc->_scale.z};
                    if (ImGui::DragFloat3("Scale", scale, 0.1f, 0.1f)) {
                        tc->setScale(glm::vec3(scale[0], scale[1], scale[2]));
                    }
                }

                tc->bDirty = true;
            }
        }
        imcDrawMaterials();
    }
    imgui.endFrame();
    imgui.render();
    // TODO: Abstract ImGui submission for multi-backend support
    if (render->getAPI() == ERenderAPI::Vulkan) {
        imgui.submitVulkan(curCmdBuf->getHandleAs<VkCommandBuffer>());
    }


#pragma endregion

    // TODO: subpasses?
    _rt->end(curCmdBuf.get());
    curCmdBuf->end();

    render->end(imageIndex, {cmdBufHandle});
}


void App::onRenderGUI()
{
    _rt->onRenderGUI();
}

// MARK: Scene
bool App::loadScene(const std::string &path)
{
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

void App::onSceneInit(Scene *scene)
{
    // Engine core initialization - basic scene setup
    // Application-specific logic should be in derived classes (e.g., HelloMaterial)

    // Set up default camera
    camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    // Create camera entity
    auto cam = scene->createEntity("Camera");
    cam->addComponent<TransformComponent>();
    cam->addComponent<CameraComponent>();
    cam->addComponent<SimpleMaterialComponent>();
    _rt->setCamera(cam);

    YA_CORE_ASSERT(scene->getRegistry().any_of<CameraComponent>(cam->getHandle()), "Camera component not found");
    YA_CORE_ASSERT(cam->hasComponent<CameraComponent>(), "Camera component not attached");

    auto cc    = cam->getComponent<CameraComponent>();
    auto owner = cc->getOwner();
    YA_CORE_ASSERT(owner == cam, "Camera component owner mismatch");
}

bool App::onWindowResized(const WindowResizeEvent &event)
{
    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", w, h, aspectRatio);
    camera.setAspectRatio(aspectRatio);
    _windowSize = {w, h};
    return false;
}

bool App::onKeyReleased(const KeyReleasedEvent &event)
{
    if (event.getKeyCode() == EKey::Escape) {
        YA_CORE_INFO("{}", event.toString());
        requestQuit();
    }
    return true;
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
    float sensitivity = 0.5f;
    auto  cam         = _rt->getCameraMut();
    auto  cc          = cam->getComponent<CameraComponent>();
    // up is +, and down is -, up is close so -=
    cc->_distance = cc->_distance -= event._offsetY * sensitivity;
    return false;
}

void App::imcDrawMaterials()
{
    if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Indent();

    uint32_t materialIdx = 0;

    auto simpleMaterials = MaterialFactory::get()->getMaterials<SimpleMaterial>();
    for (auto &mat : simpleMaterials) {
        auto simpleMat = mat->as<SimpleMaterial>();
        ImGui::PushID(std::format("Material_{}", materialIdx).c_str());
        if (ImGui::CollapsingHeader(std::format("Material{} ({})", materialIdx, simpleMat->getLabel()).c_str())) {
            int colorType = static_cast<int>(simpleMat->colorType);
            if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
                simpleMat->colorType = static_cast<SimpleMaterial::EColor>(colorType);
            }
        }
        ImGui::PopID();
        materialIdx += 1;
    }
    auto unlitMaterials = MaterialFactory::get()->getMaterials<UnlitMaterial>();

    // auto type1 = MaterialFactory::getTypeID<UnlitMaterial>();
    // auto type2 = MaterialFactory::getTypeID<SimpleMaterial>();
    // ImGui::Text("Count %d, %d", simpleMaterials.size(), unlitMaterials.size());
    // ImGui::Text("TypeID %zu, %zu", type1, type2);


    for (auto &mat : unlitMaterials) {
        ImGui::PushID(std::format("Material_{}", materialIdx).c_str());
        auto unlitMat = mat->as<UnlitMaterial>();
        if (ImGui::CollapsingHeader(std::format("Material{} ({})", materialIdx, unlitMat->getLabel()).c_str())) {
            bool bDirty = false;
            bDirty |= ImGui::DragFloat3("Base Color0", glm::value_ptr(unlitMat->uMaterial.baseColor0), 0.1f);
            bDirty |= ImGui::DragFloat3("Base Color1", glm::value_ptr(unlitMat->uMaterial.baseColor1), 0.1f);
            bDirty |= ImGui::DragFloat("Mix Value", &unlitMat->uMaterial.mixValue, 0.01f, 0.0f, 1.0f);
            for (uint32_t i = 0; i < unlitMat->_textureViews.size(); i++) {
                // if (ImGui::CollapsingHeader(std::format("Texture{}", i).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                auto &tv    = unlitMat->_textureViews[i];
                auto  label = tv.texture->getLabel();
                if (label.empty()) {
                    label = tv.texture->getFilepath();
                }
                ImGui::Text("Texture %d: %s", i, label.c_str());
                bDirty |= ImGui::Checkbox(std::format("Enable##{}", i).c_str(), &tv.bEnable);
                bDirty |= ImGui::DragFloat2(std::format("Offset##{}", i).c_str(), glm::value_ptr(tv.uvTranslation), 0.01f);
                bDirty |= ImGui::DragFloat2(std::format("Scale##{}", i).c_str(), glm::value_ptr(tv.uvScale), 0.01f, 0.01f, 10.0f);
                static constexpr const auto pi = glm::pi<float>();
                bDirty |= ImGui::DragFloat(std::format("Rotation##{}", i).c_str(), &tv.uvRotation, pi / 3600, -pi, pi);
            }
            if (bDirty) {
                unlitMat->setParamDirty(true);
            }
        }
        ImGui::PopID();
        materialIdx += 1;
    }

    // auto litMaterials = MaterialFactory::get()->getMaterials<LitMaterial>();
    // for (auto &mat : litMaterials) {
    //     ImGui::PushID(std::format("Material_{}", materialIdx).c_str());
    //     auto litMat = mat->as<LitMaterial>();
    //     if (ImGui::CollapsingHeader(std::format("Material{} ({})", materialIdx, litMat->getLabel()).c_str())) {
    //         bool bDirty = false;
    //         bDirty |= ImGui::ColorEdit3("Object Color", glm::value_ptr(litMat->uParams.objectColor));
    //         if (bDirty) {
    //             litMat->setParamDirty(true);
    //         }
    //     }
    //     ImGui::PopID();
    //     materialIdx += 1;
    // }

    ImGui::Unindent();
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
