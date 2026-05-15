#include "Runtime/App/Lifecycle/AppLifecycle.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Lifecycle/AppAutomation.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "Config/ConfigManager.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/FileWatcher.h"
#include "Core/System/VirtualFileSystem.h"

#include "Core/UI/UIManager.h"

#include "Editor/TypeRenderer.h"

#include "ECS/System/ComponentLinkageSystem.h"
#include "ECS/System/LuaScriptingSystem.h"
#include "ECS/System/ModelInstantiationSystem.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "ECS/System/TransformSystem.h"

#include "Resource/AssetManager.h"
#include "Resource/Font/FontManager.h"

#include "Render/Material/MaterialFactory.h"

#include "Runtime/App/Render/AnimationSystem.h"

#include "Scene/SceneManager.h"

#include <array>
#include <csignal>
#include <filesystem>
#include <utility>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ya
{
namespace
{
std::string findRuntimeDefaultFontPath()
{
    static constexpr std::array<const char*, 11> fontCandidates = {
        "Engine/Content/Fonts/NotoSansSC-Regular.otf",
        "Engine/Content/Fonts/SourceHanSansSC-Regular.otf",
        // Windows system fonts
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        // macOS system fonts (CJK-capable)
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        "Engine/Content/Fonts/JetBrainsMono-Medium.ttf",
    };

    for (const char* candidate : fontCandidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::string resolveStartupScenePath(const AppDesc& appDesc)
{
    if (appDesc.automation.scenePath) {
        return *appDesc.automation.scenePath;
    }
    return appDesc.defaultScenePath.value_or("");
}
} // namespace

void App::init(AppDesc ci)
{
    AppLifecycle::init(*this, std::move(ci));
}

void App::onInit(const AppDesc& ci)
{
    AppLifecycle::onInit(*this, ci);
}

void App::onPostInit()
{
    AppLifecycle::onPostInit(*this);
}

void App::quit()
{
    AppLifecycle::quit(*this);
}

bool App::loadScene(const std::string& path)
{
    return AppLifecycle::loadScene(*this, path);
}

bool App::unloadScene()
{
    return AppLifecycle::unloadScene(*this);
}

void App::onSceneInit(Scene* scene)
{
    AppLifecycle::onSceneInit(*this, scene);
}

void App::onSceneDestroy(Scene* scene)
{
    AppLifecycle::onSceneDestroy(*this, scene);
}

void App::onSceneActivated(Scene* scene)
{
    AppLifecycle::onSceneActivated(*this, scene);
}

void App::onEnterRuntime()
{
    AppLifecycle::onEnterRuntime(*this);
}

void App::startRuntime()
{
    AppLifecycle::startRuntime(*this);
}

void App::startSimulation()
{
    AppLifecycle::startSimulation(*this);
}

void App::stopRuntime()
{
    AppLifecycle::stopRuntime(*this);
}

void App::stopSimulation()
{
    AppLifecycle::stopSimulation(*this);
}

void AppLifecycle::init(App& app, AppDesc ci)
{
    YA_PROFILE_FUNCTION_LOG();
    app._ci = std::move(ci);
    YA_CORE_ASSERT(App::_instance == nullptr, "Only one instance of App is allowed");
    App::_instance = &app;

    handleSystemSignals(app);
    {
        YA_PROFILE_SCOPE_LOG("App Init Subsystems");
        {
            YA_PROFILE_SCOPE_LOG("Deferred Initializers");
            ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
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
        MaterialFactory::init();
    }

    auto& configManager          = ConfigManager::get();
    app._ci.bEnableRenderDoc     = app._ci.bEnableRenderDoc || configManager.getOr<bool>("engine", "enableRenderDoc", false);
    app._ci.disabledGraphicsCards = configManager.getOr<std::vector<std::string>>("engine", "disableGraphicsCards", app._ci.disabledGraphicsCards);
    if (!app._ci.defaultScenePath) {
        app._ci.defaultScenePath = configManager.getOr<std::string>("editor", "startup.defaultScenePath", "");
    }
    if (app._ci.automation.configPath && !app._ci.automation.configPath->empty()) {
        if (auto* vfs = VirtualFileSystem::get(); vfs && vfs->isFileExists(*app._ci.automation.configPath)) {
            configManager.openDocument(
                "automation",
                *app._ci.automation.configPath,
                Config::OpenDocumentOptions{
                    .bPersistIfMissing = false,
                    .bReadOnly         = true,
                });
            YA_CORE_INFO("Loaded automation override config: {}", *app._ci.automation.configPath);
        }
        else {
            YA_CORE_WARN("Automation override config not found, falling back to default settings: {}", *app._ci.automation.configPath);
        }
    }
    AppAutomation::applyStartupOverrides(app._ci);
    AppAutomation::beginRuntimeProfiling(app._ci.automation);
    if (configManager.hasDocument("automation")) {
        AppAutomation::applyRuntimeOverrides(app);
    }

    app._renderRuntime = std::make_unique<RenderRuntime>();
    app._renderRuntime->init(RenderRuntime::InitDesc{
        .app     = &app,
        .appDesc = &app._ci,
    });
    if (auto* render = app.getRender()) {
        int winW = 0, winH = 0;
        render->getWindowSize(winW, winH);
        app._windowSize.x = static_cast<float>(winW);
        app._windowSize.y = static_cast<float>(winH);
    }

    app._sceneManager = new SceneManager();
    app._sceneManager->setAppState(app._appState);
    app._sceneManager->onSceneInit.addLambda(&app, [&app](Scene* scene)
                                             { app.onSceneInit(scene); });
    app._sceneManager->onSceneActivated.addLambda(&app, [&app](Scene* scene)
                                                  { app.onSceneActivated(scene); });
    app._sceneManager->onSceneDestroy.addLambda(&app, [&app](Scene* scene)
                                                { app.onSceneDestroy(scene); });
    app._deleter.push("SceneManager", [&app](void*)
                      {
        delete app._sceneManager;
        app._sceneManager = nullptr; });

    FPSControl::get()->bEnable = true;
    FPSControl::get()->setFPSLimit(120.f);

    auto sys = ya::makeShared<ModelInstantiationSystem>();
    sys->init();
    app._systems.push_back(sys);
    auto sys2 = ya::makeShared<ResourceResolveSystem>();
    sys2->init();
    app._resourceResolveSystem = sys2.get();
    app._systems.push_back(sys2);
    auto sys3 = ya::makeShared<TransformSystem>();
    sys3->init();
    app._systems.push_back(sys3);
    auto sys4 = ya::makeShared<SkeletonAnimationSystem>();
    sys4->init();
    app._systems.push_back(sys4);
    auto sys5 = ya::makeShared<ComponentLinkageSystem>();
    sys5->init();
    app._systems.push_back(sys5);
    app._deleter.push("Systems", [&app](void*)
                      {
        for (auto& sys : app._systems) {
            sys->shutdown();
        }
        app._systems.clear();
        app._resourceResolveSystem = nullptr; });

    app._editorLayer = new EditorLayer(&app);
    app._editorLayer->onAttach();
    app._deleter.push("Editor", [&app](void*)
                      {
        app._editorLayer->onDetach();
        delete app._editorLayer;
        app._editorLayer = nullptr; });

    ya::registerBuiltinTypeRenderers();

    app._luaScriptingSystem = new LuaScriptingSystem();
    app._luaScriptingSystem->init();
    app._deleter.push("LuaScriptingSystem", [&app](void*)
                      {
        app._luaScriptingSystem->shutdown();
        delete app._luaScriptingSystem;
        app._luaScriptingSystem = nullptr; });

    {
        YA_PROFILE_SCOPE_LOG("Inheritance Init");
        app.onInit(app._ci);
    }

    {
        YA_PROFILE_SCOPE_LOG("Post Init");
        app.onPostInit();
    }

    loadScene(app, resolveStartupScenePath(app._ci));

    glm::vec3 editorCameraPosition = glm::vec3(0.0f, 0.0f, 5.0f);
    glm::vec3 editorCameraRotation = glm::vec3(0.0f, 0.0f, 0.0f);
    if (app._ci.automation.editorCameraPosition) {
        editorCameraPosition = *app._ci.automation.editorCameraPosition;
    }
    if (app._ci.automation.editorCameraRotation) {
        editorCameraRotation = *app._ci.automation.editorCameraRotation;
    }

    app.camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    app.camera.setPositionAndRotation(editorCameraPosition, editorCameraRotation);
}

void AppLifecycle::handleSystemSignals(App& app)
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

    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
#else
    (void)app;
    SetConsoleCtrlHandler(
        [](DWORD dwCtrlType) -> BOOL {
            switch (dwCtrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
                YA_CORE_INFO("Received Ctrl+C, requesting graceful shutdown...");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true;
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                YA_CORE_INFO("Received system shutdown event");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true;
            };

            return FALSE;
        },
        TRUE);
#endif
}

void AppLifecycle::onInit(App& app, const AppDesc& ci)
{
    (void)app;
    (void)ci;
    if (const std::string runtimeFontPath = findRuntimeDefaultFontPath(); !runtimeFontPath.empty()) {
        FontManager::get()->loadFont(runtimeFontPath, DEFAULT_RUNTIME_FONT_NAME, DEFAULT_RUNTIME_FONT_SIZE);
    }

    auto mgr = UIManager::get();
    (void)mgr;
}

void AppLifecycle::onPostInit(App& app)
{
    (void)app;
    const char* faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char* uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTextureSync("face", faceTexturePath);
    ya::AssetManager::get()->loadTextureSync("uv1", uv1TexturePath);
}

void AppLifecycle::quit(App& app)
{
    if (auto* render = app.getRender()) {
        render->waitIdle();
    }
    TaskQueue::get().stop();
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        app.onQuit();
    }

    for (auto& frameData : app._renderFrameDataPerFlight) {
        frameData.clear();
    }
    unloadScene(app);

    app._deleter.clear();

    if (app._renderRuntime) {
        app._renderRuntime->shutdown();
        app._renderRuntime.reset();
    }

    MaterialFactory::get()->destroy();
    AppAutomation::endRuntimeProfiling();
    ConfigManager::get().shutdown();
}

bool AppLifecycle::loadScene(App& app, const std::string& path)
{
    switch (app._appState) {
    case AppState::Runtime:
        stopRuntime(app);
        break;
    case AppState::Simulation:
        stopSimulation(app);
        break;
    case AppState::Editor:
        break;
    }

    if (auto* render = app.getRender()) {
        render->waitIdle();
    }

    if (app._sceneManager) {
        return app._sceneManager->loadScene(path);
    }
    return false;
}

bool AppLifecycle::unloadScene(App& app)
{
    if (auto* render = app.getRender()) {
        render->waitIdle();
    }

    if (app._sceneManager) {
        return app._sceneManager->unloadScene();
    }
    return false;
}

void AppLifecycle::onSceneInit(App& app, Scene* scene)
{
    (void)app;
    (void)scene;
}

void AppLifecycle::onSceneDestroy(App& app, Scene* scene)
{
    (void)scene;

    for (auto& frameData : app._renderFrameDataPerFlight) {
        frameData.clear();
    }

    if (app._renderRuntime) {
        app._renderRuntime->resetSkyboxPool();
        app._renderRuntime->resetEnvironmentLightingPool();
    }
}

void AppLifecycle::onSceneActivated(App& app, Scene* scene)
{
    uint64_t selectedUUID = app._editorLayer ? app._editorLayer->getSelectedEntityUUID() : 0;

    if (app._editorLayer) {
        app._editorLayer->setSceneContext(scene);

        Entity* remappedEntity = nullptr;
        if (scene && selectedUUID != 0) {
            remappedEntity = scene->getEntityByUUID(selectedUUID);
        }
        app._editorLayer->selectEntity(remappedEntity);
    }
}

void AppLifecycle::onEnterRuntime(App& app)
{
    (void)app;
}

void AppLifecycle::startRuntime(App& app)
{
    if (app._appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start runtime: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting runtime...");
    if (!app._sceneManager || !app._sceneManager->enterPlayMode(AppState::Runtime)) {
        YA_CORE_ERROR("Failed to enter runtime mode");
        return;
    }

    app._appState = AppState::Runtime;
    if (app._sceneManager) {
        app._sceneManager->setAppState(app._appState);
    }

    app.onEnterRuntime();
}

void AppLifecycle::startSimulation(App& app)
{
    if (app._appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start simulation: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting simulation...");
    if (!app._sceneManager || !app._sceneManager->enterPlayMode(AppState::Simulation)) {
        YA_CORE_ERROR("Failed to enter simulation mode");
        return;
    }

    app._appState = AppState::Simulation;
    if (app._sceneManager) {
        app._sceneManager->setAppState(app._appState);
    }

    app.onEnterSimulation();
}

void AppLifecycle::stopRuntime(App& app)
{
    if (app._appState != AppState::Runtime) {
        YA_CORE_WARN("Cannot stop: not in runtime mode");
        return;
    }

    YA_CORE_INFO("Stopping runtime");
    if (auto* render = app.getRender()) {
        render->waitIdle();
    }
    if (app._sceneManager) {
        app._sceneManager->exitPlayMode();
    }
    app._appState = AppState::Editor;
    if (app._sceneManager) {
        app._sceneManager->setAppState(app._appState);
    }
    if (app._luaScriptingSystem) {
        app._luaScriptingSystem->onStop();
    }
}

void AppLifecycle::stopSimulation(App& app)
{
    if (app._appState != AppState::Simulation) {
        YA_CORE_WARN("Cannot stop: not in simulation mode");
        return;
    }

    YA_CORE_INFO("Stopping simulation");
    if (auto* render = app.getRender()) {
        render->waitIdle();
    }
    if (app._sceneManager) {
        app._sceneManager->exitPlayMode();
    }
    app._appState = AppState::Editor;
    if (app._sceneManager) {
        app._sceneManager->setAppState(app._appState);
    }

    app.onExitSimulation();
}

} // namespace ya
