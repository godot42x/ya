#include "Runtime/App/App.h"


// Core
#include "Core/Debug/RenderDocCapture.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/FileWatcher.h"
#include "Core/System/VirtualFileSystem.h"

#include "Config/ConfigManager.h"

#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/SDLMisc.h"



// Managers/System
#include "Core/Manager/Facade.h"

#include "ImGuiHelper.h"
#include "Render/Core/FrameBuffer.h"
#include "Scene/SceneManager.h"

#include "Resource/AssetManager.h"
#include "Resource/PrimitiveMeshCache.h"
// #include "Resource/RenderTargetPool.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/TextureLibrary.h"


// Render Core
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"



// ECS

#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/PlayerComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/2D/UIComponentSytstem.h"
#include "ECS/System/ComponentLinkageSystem.h"
#include "ECS/System/LuaScriptingSystem.h"

#include "ECS/System/ModelInstantiationSystem.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "ECS/System/TransformSystem.h"



// Render
#include "Platform/Render/Vulkan//VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
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


namespace ya
{

// Define the static member variable
App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;



// ===== TODO: These global variables should be moved to appropriate managers =====



ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

App::~App() = default;



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
            ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
            profiling::StaticInitProfiler::recordEnd();
        }
        VirtualFileSystem::init();
        ConfigManager::get().init();
        ConfigManager::get().openDocument(
            "engine",
            "Engine/Config/Engine.jsonc",
            Config::OpenDocumentOptions{
                .bPersistIfMissing = false,
                .bReadOnly         = true,
            });
        ConfigManager::get().openDocument("editor", "Engine/Saved/Config/Editor.json");
        Logger::init();
        FileWatcher::init();
        // 反射系统已通过静态初始化自动注册（YA_REFLECT 宏）
        MaterialFactory::init();
    }

    auto& configManager       = ConfigManager::get();
    _ci.bEnableRenderDoc      = _ci.bEnableRenderDoc || configManager.getOr<bool>("engine", "enableRenderDoc", false);
    _ci.disabledGraphicsCards = configManager.getOr<std::vector<std::string>>("engine", "disableGraphicsCards", _ci.disabledGraphicsCards);

    _renderRuntime = std::make_unique<RenderRuntime>();
    _renderRuntime->init(RenderRuntime::InitDesc{
        .app     = this,
        .appDesc = &_ci,
    });
    if (auto* render = getRender()) {
        int winW = 0, winH = 0;
        render->getWindowSize(winW, winH);
        _windowSize.x = static_cast<float>(winW);
        _windowSize.y = static_cast<float>(winH);
    }


    _sceneManager = new SceneManager();
    _sceneManager->onSceneInit.addLambda(this, [this](Scene* scene) { this->onSceneInit(scene); });
    _sceneManager->onSceneActivated.addLambda(this, [this](Scene* scene) { this->onSceneActivated(scene); });
    _sceneManager->onSceneDestroy.addLambda(this, [this](Scene* scene) { this->onSceneDestroy(scene); });
    _deleter.push("SceneManager", [this](void*) {
        delete _sceneManager;
        _sceneManager = nullptr;
    });


    FPSControl::get()->bEnable = true;
    FPSControl::get()->setFPSLimit(120.f);

    auto sys = ya::makeShared<ModelInstantiationSystem>();
    sys->init();
    _systems.push_back(sys);
    auto sys2 = ya::makeShared<ResourceResolveSystem>();
    sys2->init();
    _systems.push_back(sys2);
    auto sys3 = ya::makeShared<TransformSystem>();
    sys3->init();
    _systems.push_back(sys3);
    auto sys4 = ya::makeShared<ComponentLinkageSystem>();
    sys4->init();
    _systems.push_back(sys4);
    _deleter.push("Systems", [this](void*) {
        for (auto& sys : _systems) {
            sys->shutdown();
        }
        _systems.clear();
    });


    _editorLayer = new EditorLayer(this);
    _editorLayer->onAttach();
    // todo: use ref
    // _editorLayer->onViewportResized.set([this](Rect2D rect) {
    //     onSceneViewportResized(rect);
    // });
    _deleter.push("Editor", [this](void*) {
        _editorLayer->onDetach();
        delete _editorLayer;
        _editorLayer = nullptr;
    });

    // see TypeRenderer.h
    ya::registerBuiltinTypeRenderers();

    // _viewportRT->onFrameBufferRecreated.addLambda(this, [this]() {
    //     YA_CORE_INFO("EditorLayer: viewport RT recreated, cleaning up ImGui texture cache");
    //     auto imageView = _viewportRT->getFrameBuffer()->getImageView(0);
    //     _editorLayer->setViewportImage(imageView);
    // });
    _luaScriptingSystem = new LuaScriptingSystem();
    _luaScriptingSystem->init();
    _deleter.push("LuaScriptingSystem", [this](void*) {
        _luaScriptingSystem->shutdown();
        delete _luaScriptingSystem;
        _luaScriptingSystem = nullptr;
    });


    {
        YA_PROFILE_SCOPE_LOG("Inheritance Init");
        onInit(ci);
    }

    {
        YA_PROFILE_SCOPE_LOG("Post Init");
        onPostInit();
    }


    loadScene(ci.defaultScenePath);

    camera.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
}

template <typename T>
int App::dispatchEvent(const T& event)
{
    if (0 == onEvent(event)) {
        MessageBus::get()->publish(event);
    }
    return 0;
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
    const char* faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char* uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTextureSync("face", faceTexturePath);
    ya::AssetManager::get()->loadTextureSync("uv1", uv1TexturePath);

    onScenePostInit.broadcast();
}


int App::onEvent(const Event& event)
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
        bHandled |= onMouseMoved(static_cast<const MouseMoveEvent&>(event));

    } break;
    case EEvent::MouseButtonReleased:
    {
        bHandled |= onMouseButtonReleased(static_cast<const MouseButtonReleasedEvent&>(event));
    } break;
    case EEvent::WindowResize:
    {
        bHandled |= onWindowResized(static_cast<const WindowResizeEvent&>(event));
    } break;
    case EEvent::KeyReleased:
    {
        bHandled |= onKeyReleased(static_cast<const KeyReleasedEvent&>(event));
    } break;
    case EEvent::MouseScrolled:
    {
        bHandled |= onMouseScrolled(static_cast<const MouseScrolledEvent&>(event));
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

    Rect2D viewportRect = _renderRuntime ? _renderRuntime->getViewportRect() : Rect2D{};
    bool   bInViewport  = FUIHelper::isPointInRect(_lastMousePos, viewportRect.pos, viewportRect.extent);
    // currently ui only rendering in viewport
    if (bInViewport)
    {
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
    if (auto* render = getRender()) {
        render->waitIdle();
    }
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        onQuit();
    }

    unloadScene();

    // CRITICAL: Destroy SceneManager BEFORE LuaScriptingSystem
    // LuaScriptComponent holds sol::object references to lua state
    // If lua state is destroyed first, component destruction will crash
    //
    // CRITICAL: Destroy SceneManager (via _deleter) BEFORE MaterialFactory
    // MaterialComponent destructors call MaterialFactory::destroyMaterial()
    // If MaterialFactory is destroyed first, component destruction will crash
    _deleter.clear();

    // CRITICAL: Shutdown render pipelines BEFORE MaterialFactory
    // DeferredRenderPipeline holds material references (fallback material, matPools)
    // that must be released before MaterialFactory destroys all materials.
    if (_renderRuntime) {
        _renderRuntime->shutdown();
        _renderRuntime.reset();
    }

    MaterialFactory::get()->destroy();
    ConfigManager::get().shutdown();
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


int ya::App::processEvent(SDL_Event& event)
{
    processSDLEvent(
        event,
        [this](const auto& e) { this->dispatchEvent(e); });
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
    ++_frameIndex;
    return 0;
}


// MARK: update
void App::tickLogic(float dt)
{
    YA_PROFILE_FUNCTION()
    taskManager.update();
    Facade.timerManager.onUpdate(dt);
    syncViewportState();

    // Store real-time delta for editor
    // Note: ClearValue is now set in onRender() via beginRenderPass() or beginRendering()

    for (auto& sys : _systems) {
        sys->onUpdate(dt);
    }


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
    if (auto* watcher = FileWatcher::get()) {
        watcher->poll();
    }

    _editorLayer->onUpdate(dt);
    inputManager.postUpdate();



    inputManager.preUpdate();
    // Update Editor camera (FreeCamera)
    cameraController.update(camera, inputManager, dt);

    auto* render = getRender();
    if (!render) {
        return;
    }
    auto        vkRender       = render->as<VulkanRender>();
    auto        windowProvider = vkRender->_windowProvider;
    std::string title          = std::format("{}({})", _ci.title, vkRender->_selectedDeviceInfo.deviceName);
    SDL_SetWindowTitle(windowProvider->getNativeWindowPtr<SDL_Window>(), title.c_str());

    prepareRenderFrameState(dt);
}

void App::syncViewportState()
{
    auto* renderRuntime = getRenderRuntime();
    if (!renderRuntime || !_editorLayer) {
        return;
    }

    Rect2D pendingRect;
    if (!_editorLayer->getPendingViewportResize(pendingRect)) {
        return;
    }

    renderRuntime->onViewportResized(pendingRect);
    if (pendingRect.extent.x > 0 && pendingRect.extent.y > 0) {
        camera.setAspectRatio(pendingRect.extent.x / pendingRect.extent.y);
    }
}

Extent2D App::resolveViewportExtent(RenderRuntime* renderRuntime, const Rect2D& viewportRect) const
{
    if (renderRuntime) {
        Extent2D extent = renderRuntime->getViewportExtent();
        if (extent.width > 0 && extent.height > 0) {
            return extent;
        }
    }

    if (viewportRect.extent.x > 0 && viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(viewportRect.extent);
    }

    return Extent2D{
        .width  = static_cast<uint32_t>(_windowSize.x),
        .height = static_cast<uint32_t>(_windowSize.y),
    };
}

void App::prepareRenderFrameState(float dt)
{
    auto* renderRuntime = getRenderRuntime();
    if (!renderRuntime) {
        _renderFrameState = {};
        return;
    }

    Rect2D viewportRect = renderRuntime->getViewportRect();

    Entity* runtimeCamera = getPrimaryCamera();
    if (runtimeCamera && runtimeCamera->isValid()) {
        auto     cc             = runtimeCamera->getComponent<CameraComponent>();
        auto     tc             = runtimeCamera->getComponent<TransformComponent>();
        Extent2D viewportExtent = resolveViewportExtent(renderRuntime, viewportRect);
        cameraController.update(*tc, *cc, inputManager, viewportExtent, dt);
        if (viewportExtent.height > 0) {
            cc->setAspectRatio(static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height));
        }
    }

    const bool bUseRuntimeCamera = _appState == AppState::Runtime &&
                                   runtimeCamera && runtimeCamera->isValid() &&
                                   runtimeCamera->hasComponent<CameraComponent>();

    _renderFrameState.viewportRect             = viewportRect;
    _renderFrameState.viewportFrameBufferScale = renderRuntime->getViewportFrameBufferScale();
    if (bUseRuntimeCamera) {
        auto cc                      = runtimeCamera->getComponent<CameraComponent>();
        auto tc                      = runtimeCamera->getComponent<TransformComponent>();
        _renderFrameState.view       = cc->getFreeView();
        _renderFrameState.projection = cc->getProjection();
        _renderFrameState.cameraPos  = tc->getWorldPosition();
        return;
    }

    _renderFrameState.view       = camera.getViewMatrix();
    _renderFrameState.projection = camera.getProjectionMatrix();
    _renderFrameState.cameraPos  = camera.getPosition();
}

// MARK: Render
void App::tickRender(float dt)
{
    auto* renderRuntime = getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    renderRuntime->renderFrame(RenderRuntime::FrameInput{
        .dt                       = dt,
        .sceneManager             = _sceneManager,
        .editorLayer              = _editorLayer,
        .viewportRect             = _renderFrameState.viewportRect,
        .viewportFrameBufferScale = _renderFrameState.viewportFrameBufferScale,
        .appMode                  = _appMode,
        .clicked                  = &clicked,
        .view                     = _renderFrameState.view,
        .projection               = _renderFrameState.projection,
        .cameraPos                = _renderFrameState.cameraPos,
    });
}

// MARK: Render GUI
void App::onRenderGUI(float dt)
{
    YA_PROFILE_FUNCTION()
    auto& io = ImGui::GetIO();
    (void)io;
    if (!ImGui::Begin("App Info"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Render 2D", 0)) {
        // temp code here to adopt to new Render2D
        Render2D::onImGui();
    }

    // if (ImGui::CollapsingHeader("Render Target Pool", 0)) {
    //     // RenderTargetPool::get().onRenderGUI();
    // }

    if (_renderRuntime) {
        _renderRuntime->renderGUI(dt);
    }

    if (ImGui::CollapsingHeader("Context", ImGuiTreeNodeFlags_DefaultOpen)) {

        {
            static constexpr int ringBufSize = 120;                          // 环形缓冲区大小，保持原有定义
            static float         fpsRingBuf[ringBufSize]{};                  // 存储每帧的FPS值
            static int           fpsRingHead = 0;                            // 缓冲区头指针（下一个要写入的位置）
            static int           fpsRingFill = 0;                            // 缓冲区已填充的元素数量
            static float         fpsSum      = 0.0f;                         // 新增：维护缓冲区所有元素的累加和，避免每次遍历求和
            float                currentFps  = dt > 0.0f ? 1.0f / dt : 0.0f; // 计算当前帧的FPS（dt为帧间隔时间，单位：秒）

            // 1. 处理旧值：缓冲区满时，先减去即将被覆盖的旧值
            if (fpsRingFill >= ringBufSize) {
                fpsSum -= fpsRingBuf[fpsRingHead]; // head指向的就是即将被覆盖的最旧元素
            }
            // 2. 写入新值并更新累加和
            fpsRingBuf[fpsRingHead] = currentFps;
            fpsSum += currentFps;

            // 3. 更新环形缓冲区指针和填充数
            fpsRingHead = (fpsRingHead + 1) % ringBufSize;
            fpsRingFill = std::min(fpsRingFill + 1, ringBufSize);

            // 4. 计算平均FPS（O(1)时间复杂度）
            float avgFps = fpsRingFill > 0 ? (fpsSum / static_cast<float>(fpsRingFill)) : 0.0f;

            // 输出FPS信息（简化当前帧FPS的获取，避免重复计算索引）
            ImGui::Text("%s",
                        std::format("Frame: {}, DeltaTime: {:.2f} ms,\t FPS: {:.1f} (avg {}f: {:.1f})",
                                    _frameIndex,
                                    dt * 1000.0f,
                                    currentFps,
                                    fpsRingFill,
                                    avgFps)
                            .data());
        }


        static int count = 0;
        if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
            count++;
            YA_CORE_INFO("=====================================");
        }

        if (ImGui::TreeNode("ImGUI")) {
            ImGuiManager::get().onRenderGUI();
            ImGui::TreePop();
        }

        auto* render = getRender();
        YA_CORE_ASSERT(render, "Render is null");
        auto* swapchain = render->getSwapchain();
        bool  bVsync    = swapchain->getVsync();
        if (ImGui::Checkbox("VSync", &bVsync)) {
            swapchain->setVsync(bVsync);
        }

        EPresentMode::T presentMode  = swapchain->getPresentMode();
        const char*     presentModes = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
        if (ImGui::Combo("Present Mode", reinterpret_cast<int*>(&presentMode), presentModes)) {
            taskManager.registerFrameTask([swapchain, presentMode]() {
                swapchain->setPresentMode(presentMode);
            });
        }

        AppMode mode = _appMode;
        if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
            _appMode = mode;
        }

        std::string clickedPoints;
        for (const auto& p : clicked) {
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
bool App::loadScene(const std::string& path)
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

void App::onSceneInit(Scene* scene)
{

    // Create camera entity
}

void App::onSceneDestroy(Scene* scene)
{
    // Drop runtime skybox descriptor bindings so scene-local cubemap textures
    // are no longer referenced after the scene is destroyed.
    if (_renderRuntime) {
        _renderRuntime->resetSkyboxPool();
    }
}

void App::onSceneActivated(Scene* scene)
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

Entity* App::getPrimaryCamera() const
{
    if (!_sceneManager) {
        return nullptr;
    }

    Scene* scene = _sceneManager->getActiveScene();
    if (!scene || !scene->isValid()) {
        return nullptr;
    }

    auto& registry = scene->getRegistry();

    // 1: Find first camera with PlayerComponent (fallback)
    for (const auto& [entity, cameraComp, playerComp] :
         registry.view<CameraComponent, PlayerComponent>().each()) {
        return scene->getEntityByEnttID(entity);
    }

    // 2: Find camera with _primary == true
    for (const auto& [entity, cameraComp] :
         registry.view<CameraComponent>().each()) {
        if (cameraComp.bPrimary) {
            return scene->getEntityByEnttID(entity);
        }
    }

    // No primary camera found
    return nullptr;
}

IRender* App::getRender() const
{
    return _renderRuntime ? _renderRuntime->getRender() : nullptr;
}

std::shared_ptr<ShaderStorage> App::getShaderStorage() const
{
    return _renderRuntime ? _renderRuntime->getShaderStorage() : nullptr;
}

ForwardRenderPipeline* App::getForwardPipeline() const
{
    return _renderRuntime ? _renderRuntime->getForwardPipeline() : nullptr;
}

bool App::isShadowMappingEnabled() const
{
    return _renderRuntime && _renderRuntime->isShadowMappingEnabled();
}

bool App::isMirrorRenderingEnabled() const
{
    return _renderRuntime && _renderRuntime->isMirrorRenderingEnabled();
}

bool App::hasMirrorRenderResult() const
{
    return _renderRuntime && _renderRuntime->hasMirrorRenderResult();
}

IRenderTarget* App::getShadowDepthRT() const
{
    return _renderRuntime ? _renderRuntime->getShadowDepthRT() : nullptr;
}

IImageView* App::getShadowDirectionalDepthIV() const
{
    return _renderRuntime ? _renderRuntime->getShadowDirectionalDepthIV() : nullptr;
}

IImageView* App::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    return _renderRuntime ? _renderRuntime->getShadowPointFaceDepthIV(pointLightIndex, faceIndex) : nullptr;
}

Texture* App::getPostprocessOutputTexture() const
{
    return _renderRuntime ? _renderRuntime->getPostprocessOutputTexture() : nullptr;
}

bool App::isPostprocessingEnabled() const
{
    return _renderRuntime && _renderRuntime->isPostprocessingEnabled();
}

void App::renderGUI(float dt)
{
    _editorLayer->onImGuiRender([this, dt]() {
        this->onRenderGUI(dt);
    });
}



} // namespace ya
