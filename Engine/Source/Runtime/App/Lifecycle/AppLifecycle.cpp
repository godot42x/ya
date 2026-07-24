#include "Runtime/App/Lifecycle/AppLifecycle.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Lifecycle/AppAutomation.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "Config/ConfigManager.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Profiling/StaticInitProfiler.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/FileWatcher.h"
#include "Core/System/VirtualFileSystem.h"

#include "Core/UI/UIManager.h"

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
#include <cstdlib>
#include <filesystem>
#include <utility>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ya
{
namespace
{
#if defined(__APPLE__)
std::filesystem::path getExecutableDir()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
}

void configureBundledVulkanRuntimeEnv()
{
    if (std::getenv("VK_ICD_FILENAMES") != nullptr) {
        return;
    }

    const auto executableDir = getExecutableDir();
    if (executableDir.empty()) {
        return;
    }

    const auto sdkRoot = executableDir / "Engine" / "ThirdParty" / "VulkanSDK";
    if (!std::filesystem::is_directory(sdkRoot)) {
        return;
    }

    std::filesystem::path selectedSdkDir;
    for (const auto& entry : std::filesystem::directory_iterator(sdkRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto sdkDir   = entry.path() / "macOS";
        const auto icdJson  = sdkDir / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
        const auto layerDir = sdkDir / "share" / "vulkan" / "explicit_layer.d";
        const auto moltenVk = sdkDir / "lib" / "libMoltenVK.dylib";
        if (std::filesystem::is_regular_file(icdJson) && std::filesystem::is_regular_file(moltenVk) &&
            std::filesystem::is_directory(layerDir)) {
            if (selectedSdkDir.empty() || entry.path().filename().string() > selectedSdkDir.parent_path().filename().string()) {
                selectedSdkDir = sdkDir;
            }
        }
    }

    if (selectedSdkDir.empty()) {
        return;
    }

    const auto icdJson  = selectedSdkDir / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
    const auto layerDir = selectedSdkDir / "share" / "vulkan" / "explicit_layer.d";
    const auto sdkPath  = selectedSdkDir.string();

    setenv("VULKAN_SDK", sdkPath.c_str(), 0);
    setenv("VK_ICD_FILENAMES", icdJson.string().c_str(), 0);
    setenv("VK_LAYER_PATH", layerDir.string().c_str(), 0);
}
#endif

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
#if defined(__APPLE__)
    configureBundledVulkanRuntimeEnv();
#endif
    {
        YA_PROFILE_SCOPE_LOG("Init Config");

        {
            YA_PROFILE_SCOPE_LOG("Deferred Initializers");
            ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
        }

        VirtualFileSystem::init();
        if (app._ci.projectRoot) {
            VirtualFileSystem::get()->setGameRoot(*app._ci.projectRoot);
        }
        ConfigManager::get().init();
        ConfigManager::get().openDocument(
            "engine",
            "Engine/Config/Engine.jsonc",
            Config::OpenDocumentOptions{
                .bPersistIfMissing = false,
                .bReadOnly         = true,
            });
        ConfigManager::get().openDocument(
            "runtime",
            "Engine/Saved/Config/Runtime.json",
            Config::OpenDocumentOptions{
                .bPersistIfMissing = true,
                .bReadOnly         = false,
            });
        profiling::StaticInitProfiler::ensureStarted();
        profiling::StaticInitProfiler::refOBJ();
        app.configureModules();
        profiling::StaticInitProfiler::recordEnd();

        AppAutomation::loadConfig(app._ci);
        Logger::init();

        auto& configManager           = ConfigManager::get();
        app._ci.bEnableRenderDoc      = app._ci.bEnableRenderDoc || configManager.getOr<bool>("engine", "enableRenderDoc", false);
        app._ci.disabledGraphicsCards = configManager.getOr<std::vector<std::string>>("engine", "disableGraphicsCards", app._ci.disabledGraphicsCards);
        profiling::applyAppOverrides(app._ci);
        AppAutomation::applyStartupOverrides(app._ci);
        AppAutomation::applyLogOverrides(app._ci);
    }

    {
        YA_PROFILE_SCOPE_LOG("Init Default");

        app.inputManager.init();
        FileWatcher::init();
        MaterialFactory::init();
        TaskQueue::get().start(/*numThreads=*/2);
        profiling::beginRuntimeSession(app._ci);
        if (ConfigManager::get().hasDocument("automation")) {
            AppAutomation::applyRuntimeOverrides(app);
        }
    }

    app._renderRuntime = std::make_unique<RenderRuntime>();
    app._renderRuntime->init(RenderRuntime::InitDesc{
        .app     = &app,
        .appDesc = &app._ci,
    });
    if (ConfigManager::get().hasDocument("automation")) {
        AppAutomation::applyRuntimeOverrides(app);
    }
    if (auto* render = app.getRender()) {
        int winW = 0, winH = 0;
        render->getWindowSize(winW, winH);
        app._windowSize.x = static_cast<float>(winW);
        app._windowSize.y = static_cast<float>(winH);
        app.inputRouter.setWindow(render->getNativeWindow<SDL_Window*>());
    }

    app._sceneManager = new SceneManager();
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

    app.attachModules();
    app._deleter.push("Modules", [&app](void*) { app.detachModules(); });

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

    const std::string startupScenePath = resolveStartupScenePath(app._ci);
    if (!startupScenePath.empty()) {
        const bool bLoadedStartupScene = loadScene(app, startupScenePath);
        if (bLoadedStartupScene && !app._ci.bEditor && app._appState == AppState::Stopped) {
            startRuntime(app);
        }
    }

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
        FontManager::get()->loadFont(runtimeFontPath, DEFAULT_RUNTIME_FONT_NAME, 16);
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
    TaskQueue::get().stop();
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        app.onQuit();
    }

    for (auto& frameData : app._renderFrameDataPerFlight) {
        frameData.clear();
    }
    const bool bHadSceneBeforeUnload = app._sceneManager && app._sceneManager->hasScene();
    unloadScene(app);

    if (!bHadSceneBeforeUnload) {
        if (auto* render = app.getRender()) {
            render->waitIdle();
        }
    }

    app._deleter.clear();

    if (app._renderRuntime) {
        app._renderRuntime->shutdown(/*bRenderAlreadyIdle=*/true);
        app._renderRuntime.reset();
    }

    MaterialFactory::get()->destroy();
    profiling::endRuntimeSession();
    ConfigManager::get().shutdown();
}

bool AppLifecycle::loadScene(App& app, const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    const bool bHasCurrentScene = app._sceneManager && app._sceneManager->hasScene();
    bool bWaitedForModeTransition = false;
    switch (app._appState) {
    case AppState::Runtime:
        stopRuntime(app);
        bWaitedForModeTransition = true;
        break;
    case AppState::Simulation:
        stopSimulation(app);
        bWaitedForModeTransition = true;
        break;
    case AppState::Stopped:
        break;
    }

    if (!bWaitedForModeTransition && bHasCurrentScene) {
        if (auto* render = app.getRender()) {
            render->waitIdle();
        }
    }

    if (app._sceneManager) {
        return app._sceneManager->loadScene(path);
    }
    return false;
}

bool AppLifecycle::unloadScene(App& app)
{
    if (app._sceneManager && app._sceneManager->hasScene()) {
        if (auto* render = app.getRender()) {
            render->waitIdle();
        }
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
    app.notifyModulesSceneDestroyed(scene);

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
    app.notifyModulesSceneActivated(scene);
}

void AppLifecycle::onEnterRuntime(App& app)
{
    (void)app;
}

void AppLifecycle::startRuntime(App& app)
{
    if (app._appState != AppState::Stopped) {
        YA_CORE_WARN("Cannot start runtime: app is not stopped");
        return;
    }

    YA_CORE_INFO("Starting runtime...");
    if (!app._sceneManager || !app._sceneManager->hasScene()) {
        YA_CORE_ERROR("Cannot start runtime without an active scene");
        return;
    }
    if (!app.notifyModulesBeforeAppStateChange(AppState::Runtime)) {
        YA_CORE_WARN("Runtime start was rejected by an app module");
        return;
    }

    const AppState previousState = app._appState;
    app._appState = AppState::Runtime;
    app.notifyModulesAfterAppStateChange(previousState);

    app.onEnterRuntime();
}

void AppLifecycle::startSimulation(App& app)
{
    if (app._appState != AppState::Stopped) {
        YA_CORE_WARN("Cannot start simulation: app is not stopped");
        return;
    }

    YA_CORE_INFO("Starting simulation...");
    if (!app._sceneManager || !app._sceneManager->hasScene()) {
        YA_CORE_ERROR("Cannot start simulation without an active scene");
        return;
    }
    if (!app.notifyModulesBeforeAppStateChange(AppState::Simulation)) {
        YA_CORE_WARN("Simulation start was rejected by an app module");
        return;
    }

    const AppState previousState = app._appState;
    app._appState = AppState::Simulation;
    app.notifyModulesAfterAppStateChange(previousState);

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
    if (!app.notifyModulesBeforeAppStateChange(AppState::Stopped)) {
        YA_CORE_WARN("Runtime stop was rejected by an app module");
        return;
    }
    const AppState previousState = app._appState;
    app._appState = AppState::Stopped;
    app.notifyModulesAfterAppStateChange(previousState);
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
    if (!app.notifyModulesBeforeAppStateChange(AppState::Stopped)) {
        YA_CORE_WARN("Simulation stop was rejected by an app module");
        return;
    }
    const AppState previousState = app._appState;
    app._appState = AppState::Stopped;
    app.notifyModulesAfterAppStateChange(previousState);

    app.onExitSimulation();
}

} // namespace ya
