#include "Host/Lifecycle/AppLifecycle.h"

#include "Host/App.h"
#include "Host/AppRenderState.h"
#include "Host/Lifecycle/AppAutomation.h"
#include "Host/Automation/AppAutomationControlService.h"
#include "Host/Utility/FPSCtrl.h"
#include "AppRuntime/AppBootstrap.h"

#include "Core/Config/ConfigManager.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Profiling/StaticInitProfiler.h"
#include "Core/Module/ProjectDescriptor.h"
#include "Core/System/FileWatcher.h"

#include "Gameplay/Linkage/LinkageFramework.h"
#include "Render/Adapters/LightBillboard/LightBillboardLinkageRule.h"
#include "Render/Adapters/Material/MaterialRenderLinkageRule.h"
#include "Gameplay/Systems/LuaScriptingSystem.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "Gameplay/Systems/JSScriptingSystem.h"
#include "Core/Scripting/ScriptApiRegistry.h"
#include "Core/Scripting/ScriptApiAsset.h"
#include "Render/Adapters/ModelInstantiationSystem.h"
#include "Render3D/Services/GameplayResourceBinding.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Services/EnvironmentLightingResultProvider.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "Gameplay/Systems/TransformSystem.h"
#include "Physics/PhysicsSystem.h"

#include "Resource/AssetManager.h"
#include "GUI/Resources/FontManager.h"

#include "Render3D/Material/MaterialFactory.h"

#include "Gameplay/Systems/AnimationSystem.h"

#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"
#include "RHI/WindowProvider.h"
#include "AppRuntime/WindowManager.h"
#include "Render3D/RenderRuntime.h"

#include <format>
#include <csignal>
#include <filesystem>
#include <fstream>
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
    static constexpr const char* fontCandidates[] = {
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

std::string resolveProjectScenePath(const App& app, const std::string& requestedPath)
{
    if (requestedPath.empty()) {
        return {};
    }

    const std::filesystem::path inputPath(requestedPath);
    if (inputPath.is_absolute() && std::filesystem::is_regular_file(inputPath)) {
        return inputPath.lexically_normal().string();
    }
    if (std::filesystem::is_regular_file(inputPath)) {
        return inputPath.lexically_normal().string();
    }

    if (app.getDesc().projectRoot) {
        const auto rootedPath = std::filesystem::path(*app.getDesc().projectRoot) / inputPath;
        if (std::filesystem::is_regular_file(rootedPath)) {
            return rootedPath.lexically_normal().string();
        }
    }

    return requestedPath;
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


std::string AppLifecycle::resolveStartupScenePath(const AppDesc& appDesc)
{
    if (appDesc.automation.scenePath) {
        return *appDesc.automation.scenePath;
    }
    return appDesc.defaultScenePath.value_or("");
}

void AppLifecycle::init(App& app, AppDesc ci)
{
    YA_PROFILE_FUNCTION_LOG();
    app._ci = std::move(ci);
    YA_CORE_ASSERT(App::_instance == nullptr, "Only one instance of App is allowed");
    App::_instance = &app;

    handleSystemSignals(app);
    {
        YA_PROFILE_SCOPE_LOG("Init Config");

        {
            YA_PROFILE_SCOPE_LOG("Process Bootstrap");
            AppBootstrap::initializeProcessCore();
            AppBootstrap::initializeVirtualFileSystem();
        }
        if (app._ci.projectPath) {
            const auto descriptor = FProjectDescriptor::load(*app._ci.projectPath);
            app.applyProjectDescriptor(descriptor);
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
        AssetManager::setFrameTaskSink([&app](std::function<void()> task)
        {
            app.getTaskManager().registerFrameTask(std::move(task));
        });
        TaskQueue::get().start(/*numThreads=*/2);
        profiling::beginRuntimeSession(app._ci);
        if (ConfigManager::get().hasDocument("automation")) {
            AppAutomation::applyRuntimeOverrides(app);
        }
    }

    app._windowManager = std::make_unique<WindowManager>();
    YA_CORE_ASSERT(app._windowManager->init(), "Failed to initialize WindowManager");

    app._renderState->runtime = std::make_unique<RenderRuntime>();
    app._renderState->runtime->init(RenderRuntime::InitDesc{
        .hostServices = &app,
        .offscreenScheduler = &app.getTaskManager(),
        // Narrow read-only environment-lighting result provider: Render3D
        // consumes derived-resource handles through this contract instead of
        // locating the processor via the App singleton.
        .environmentLightingProvider = EnvironmentLightingResultProvider{
            .resolveSceneSkyboxResource = [&app](Scene* scene) -> ImageResourceRef {
                auto* envProcessor = app.getEnvironmentLightingProcessor();
                return envProcessor ? envProcessor->resolveSceneSkyboxResource(scene) : ImageResourceRef{};
            },
            .resolveSceneEnvironmentLightingResources = [&app](Scene* scene) -> EnvironmentLightingSceneResources {
                auto* envProcessor = app.getEnvironmentLightingProcessor();
                return envProcessor ? envProcessor->resolveSceneEnvironmentLightingResources(scene)
                                    : EnvironmentLightingSceneResources{};
            },
        },
        .activeSceneProvider = [&app]() -> Scene* { return app.getSceneServices().getActiveScene(); },
        .windowWidth  = static_cast<uint32_t>(app._ci.width),
        .windowHeight = static_cast<uint32_t>(app._ci.height),
        .windowTitle  = app._ci.title,
        .bEnableRenderDoc = app._ci.bEnableRenderDoc,
        .renderDocDllPath = app._ci.renderDocDllPath,
        .renderDocCaptureOutputDir = app._ci.renderDocCaptureOutputDir,
    });
    if (ConfigManager::get().hasDocument("automation")) {
        AppAutomation::applyRuntimeOverrides(app);
    }
    if (auto* render = app.getRenderServices().getRender()) {
        int winW = 0, winH = 0;
        render->getWindowSize(winW, winH);
        app._windowSize.x = static_cast<float>(winW);
        app._windowSize.y = static_cast<float>(winH);
        app.inputRouter.setWindow(render->getWindowProvider() ? render->getWindowProvider()->getNativeWindowHandle() : nullptr);
    }

    app._sceneManager = new SceneManager();
    // Scene registers itself with the lifecycle host through the injected
    // ISceneLifecycleHost seam (scene-core); no App access from Scene.
    Scene::setLifecycleHost(app._sceneManager);
    app._sceneManager->onSceneInit.addLambda(&app, [&app](Scene* scene)
                                             { AppLifecycle::onSceneInit(app, scene); });
    app._sceneManager->onSceneActivated.addLambda(&app, [&app](Scene* scene)
                                                  { AppLifecycle::onSceneActivated(app, scene); });
    app._sceneManager->onSceneDestroy.addLambda(&app, [&app](Scene* scene)
                                                { AppLifecycle::onSceneDestroy(app, scene); });
    app._deleter.push("SceneManager", [&app](void*)
                      {
        Scene::setLifecycleHost(nullptr);
        delete app._sceneManager;
        app._sceneManager = nullptr; });

    // Restore persisted frame pacing (saved by the editor's runtime tools).
    // The defaults preserve the previous engine behavior when nothing was
    // persisted; in editor runs the "editor" document is already open here.
    FPSControl::get()->bEnable = ConfigManager::get().getOr<bool>("editor", "runtime.framePacing.enabled", true);
    FPSControl::get()->setFPSLimit(ConfigManager::get().getOr<float>("editor", "runtime.framePacing.fpsLimit", 120.0f));

    auto sys = ya::makeShared<ModelInstantiationSystem>();
    sys->setSceneProvider([&app]() -> Scene*
    {
        return app.getSceneServices().getActiveScene();
    });
    sys->init();
    app._systems.push_back(sys);
    auto sys3 = ya::makeShared<TransformSystem>();
    sys3->setSceneProvider([&app]() -> Scene*
    {
        return app.getSceneServices().getActiveScene();
    });
    sys3->init();
    app._systems.push_back(sys3);
    auto sys4 = ya::makeShared<SkeletonAnimationSystem>();
    sys4->setSceneProvider([&app]() -> Scene*
    {
        return app.getSceneServices().getActiveScene();
    });
    // World-render tick policy: poses are only consumed by the world pipeline;
    // freeze sampling while the editor 2D canvas mode disables world rendering.
    sys4->setTickPolicy([&app]()
    {
        auto* renderRuntime = app.getRenderServices().getRenderRuntime();
        return !renderRuntime || renderRuntime->isWorldSceneRenderEnabled();
    });
    sys4->init();
    app._systems.push_back(sys4);
    auto sys5 = ya::makeShared<LinkageFramework>();
    // Light billboard policy is injected here (Host owns the config source);
    // the adapter never reaches Host/Config.
    LightBillboardPolicy billboardPolicy;
    billboardPolicy.point.enabled          = ConfigManager::get().getOr<bool>("editor", "lightBillboards.point.enabled", billboardPolicy.point.enabled);
    billboardPolicy.point.screenSizePixels = ConfigManager::get().getOr<float>("editor", "lightBillboards.point.screenSizePixels", billboardPolicy.point.screenSizePixels);
    billboardPolicy.point.minWorldScale    = ConfigManager::get().getOr<float>("editor", "lightBillboards.point.minWorldScale", billboardPolicy.point.minWorldScale);
    billboardPolicy.point.texturePath      = ConfigManager::get().getOr<std::string>("editor", "lightBillboards.point.texturePath", billboardPolicy.point.texturePath);
    billboardPolicy.directional.enabled          = ConfigManager::get().getOr<bool>("editor", "lightBillboards.directional.enabled", billboardPolicy.directional.enabled);
    billboardPolicy.directional.screenSizePixels = ConfigManager::get().getOr<float>("editor", "lightBillboards.directional.screenSizePixels", billboardPolicy.directional.screenSizePixels);
    billboardPolicy.directional.minWorldScale    = ConfigManager::get().getOr<float>("editor", "lightBillboards.directional.minWorldScale", billboardPolicy.directional.minWorldScale);
    billboardPolicy.directional.texturePath      = ConfigManager::get().getOr<std::string>("editor", "lightBillboards.directional.texturePath", billboardPolicy.directional.texturePath);
    auto lightBillboardRule = ya::makeShared<LightBillboardLinkageRule>(sys5.get());
    lightBillboardRule->setPolicy(billboardPolicy);
    sys5->addRule(lightBillboardRule);
    sys5->addRule(ya::makeShared<MaterialRenderLinkageRule>(sys5.get()));
    sys5->setSceneManager(app.getSceneServices().getSceneManager());
    sys5->setFrameTaskSink([&app](std::function<void()> task)
    {
        app.getTaskManager().registerFrameTask(std::move(task));
    });
    sys5->init();
    app._systems.push_back(sys5);
    auto sysPhysics = ya::makeShared<PhysicsSystem>();
    sysPhysics->setSceneManager(app.getSceneServices().getSceneManager());
    sysPhysics->setAppStateChangedSource(&app.onAppStateChanged);
    sysPhysics->init();
    app._systems.push_back(sysPhysics);
    app._deleter.push("Systems", [&app](void*)
                      {
        for (auto& sys : app._systems) {
            sys->shutdown();
        }
        app._systems.clear(); });

    app.attachModules();
    app._deleter.push("Modules", [&app](void*) { app.detachModules(); });

    if (app._automationControlService) {
        app._automationControlService->init(app._ci.automation.controlPort);
    }

    app._luaScriptingSystem = new LuaScriptingSystem();
    app._luaScriptingSystem->setRuntimeServices({
        .input           = &app.getInputManager(),
        .isMouseCaptured = [&app]() { return app.getInputRouter().isMouseCaptured(); },
        .elapsedSeconds  = [&app]() { return static_cast<double>(app.getElapsedTimeMS()) / 1000.0; },
        .frameIndex      = [&app]() { return app.getFrameIndex(); },
        .activeScene     = [&app]() -> Scene* { return app.getSceneServices().getActiveScene(); },
    });
    app._luaScriptingSystem->init();
    app._deleter.push("LuaScriptingSystem", [&app](void*)
                      {
        app._luaScriptingSystem->shutdown();
        delete app._luaScriptingSystem;
        app._luaScriptingSystem = nullptr; });

    RayCastMousePickingSystem::setAppStateProvider([]() {
        return App::get() ? App::get()->getAppState() : AppState::Stopped;
    });

    // Engine-level JS scripting: the ScriptApiRegistry is the single capability
    // catalog shared by JS scripts and the automation RPC. Providers wire the
    // registry to this app's scene services.
    {
        auto& api = ScriptApiRegistry::get();
        api.setActiveSceneProvider([&app]() -> Scene* { return app.getSceneServices().getActiveScene(); });
        api.setSaveSceneFn([&app](const std::string& path, Scene& scene) -> bool
                           { return app._sceneManager->serializeToFile(path, &scene); });
        api.setLoadSceneFn([&app](const std::string& path) -> bool { return AppLifecycle::loadScene(app, path); });

        // The app layer owns the script API catalog (scene/entity/component
        // authoring + asset APIs); JSScriptingSystem only binds the registry.
        registerCoreScriptApis(api);
        registerAssetScriptApis(api);
    }
    app._jsScriptingSystem = new JSScriptingSystem();
    app._jsScriptingSystem->init();
    app._deleter.push("JSScriptingSystem", [&app](void*)
                      {
        app._jsScriptingSystem->shutdown();
        delete app._jsScriptingSystem;
        app._jsScriptingSystem = nullptr; });

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
    // Forward font atlas textures produced by the GUI framework into the
    // engine's asset registry (dependency inversion: the GUI stays decoupled
    // from AssetManager; the host wires the two together).
    FontManager::setFontAtlasTextureSink(
        [](const FName& fontName, uint32_t fontSize, const std::shared_ptr<Texture>& atlasTexture) {
            AssetManager::get()->registerTexture(std::format("FontAtlas_{}:{}", fontName.toString(), fontSize),
                                                 atlasTexture);
        });
    if (const std::string runtimeFontPath = findRuntimeDefaultFontPath(); !runtimeFontPath.empty()) {
        auto* render = app.getRenderServices().getRender();
        YA_CORE_ASSERT(render, "AppLifecycle::onInit requires a render backend");
        for (const uint32_t fontSize : {12u, 13u, 14u, 15u, 16u, DEFAULT_RUNTIME_FONT_SIZE}) {
            FontManager::get()->loadFont(*render, runtimeFontPath, DEFAULT_RUNTIME_FONT_NAME, fontSize);
        }
    }

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
    if (app._automationControlService) {
        app._automationControlService->shutdown();
    }
    TaskQueue::get().stop();
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        app.onQuit();
    }

    for (auto& frameData : app._renderState->frameDataPerFlight) {
        frameData.clear();
    }
    const bool bHadSceneBeforeUnload = app._sceneManager && app._sceneManager->hasScene();
    unloadScene(app);

    if (!bHadSceneBeforeUnload) {
        if (auto* render = app.getRenderServices().getRender()) {
            render->waitIdle();
        }
    }

    app._deleter.clear();

    if (app._renderState->runtime) {
        app._renderState->runtime->shutdown(/*bRenderAlreadyIdle=*/true);
        app._renderState->runtime.reset();
    }
    if (app._windowManager) {
        app._windowManager->shutdown();
        app._windowManager.reset();
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
        if (auto* render = app.getRenderServices().getRender()) {
            render->waitIdle();
        }
    }

    if (app._sceneManager) {
        return app._sceneManager->loadScene(resolveProjectScenePath(app, path));
    }
    return false;
}

bool AppLifecycle::unloadScene(App& app)
{
    if (app._sceneManager && app._sceneManager->hasScene()) {
    if (auto* render = app.getRenderServices().getRender()) {
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
    if (scene && app.getGameUIHost()) {
        app.getGameUIHost()->onSceneDeactivated(*scene);
    }

    app.notifyModulesSceneDestroyed(scene);

    for (auto& frameData : app._renderState->frameDataPerFlight) {
        frameData.clear();
    }

    if (app._renderState->runtime) {
        app._renderState->runtime->resetSkyboxPool();
        app._renderState->runtime->resetEnvironmentLightingPool();
    }
}

void AppLifecycle::onSceneActivated(App& app, Scene* scene)
{
    if (scene && app.getGameUIHost()) {
        app.getGameUIHost()->onSceneActivated(*scene);
    }

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
    app.setInputMode(EInputMode::GameAndUI); // fresh session defaults
    app.onAppStateChanged.broadcast(app._appState);
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
    app.setInputMode(EInputMode::GameAndUI);
    app.onAppStateChanged.broadcast(app._appState);
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
    if (auto* render = app.getRenderServices().getRender()) {
        render->waitIdle();
    }
    if (app._luaScriptingSystem) {
        app._luaScriptingSystem->onStop();
    }
    if (!app.notifyModulesBeforeAppStateChange(AppState::Stopped)) {
        YA_CORE_WARN("Runtime stop was rejected by an app module");
        return;
    }
    const AppState previousState = app._appState;
    app._appState = AppState::Stopped;
    app.onAppStateChanged.broadcast(app._appState);
    app.getSceneServices().refreshActiveSceneDerivedState();
    app.notifyModulesAfterAppStateChange(previousState);
}

void AppLifecycle::stopSimulation(App& app)
{
    if (app._appState != AppState::Simulation) {
        YA_CORE_WARN("Cannot stop: not in simulation mode");
        return;
    }

    YA_CORE_INFO("Stopping simulation");
    if (auto* render = app.getRenderServices().getRender()) {
        render->waitIdle();
    }
    if (app._luaScriptingSystem) {
        app._luaScriptingSystem->onStop();
    }
    if (!app.notifyModulesBeforeAppStateChange(AppState::Stopped)) {
        YA_CORE_WARN("Simulation stop was rejected by an app module");
        return;
    }
    const AppState previousState = app._appState;
    app._appState = AppState::Stopped;
    app.onAppStateChanged.broadcast(app._appState);
    app.getSceneServices().refreshActiveSceneDerivedState();
    app.notifyModulesAfterAppStateChange(previousState);

    app.onExitSimulation();
}

} // namespace ya
