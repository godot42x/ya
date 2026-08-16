#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/IRuntimeModule.h"
#include "GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.h"
#include "GameRuntime/Lifecycle/HostSdlEventSource.h"
#include "GUI/Host/NativeWindowManager.h"
#include "App/Kernel/AppKernel.h"
#include "Core/Config/ConfigManager.h"
#include "Render3D/RenderRuntime.h"

#include "App/Module/ProjectDescriptor.h"
#include "Core/Profiling/Profiling.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Core/GameMounts.h"
#include "Scene/Core/Scene.h"
#include "Render3D/Services/DebugRenderSystem.h"

namespace ya
{
namespace
{
inline const FName kContentMount = game::mounts::Content;
inline const FName kGameRootMount = game::mounts::GameRoot;

/// Resolve a module's runtime hooks. Modules that only implement the generic
/// load lifecycle get a shared no-op instance, so call sites never null-check.
IRuntimeModule* getRuntimeModule(IModule* module)
{
    if (auto* runtime = static_cast<IRuntimeModule*>(module->queryInterface(YA_RUNTIME_MODULE_INTERFACE))) {
        return runtime;
    }
    static IRuntimeModule s_default;
    return &s_default;
}

class GameRuntimeLoopDelegate final : public IAppLoopDelegate
{
  public:
    explicit GameRuntimeLoopDelegate(App& inApp)
        : app(inApp)
    {
    }

    void onInit() override
    {
    }

    void onEvent(const Event& event) override
    {
        app.dispatchEvent(event);
    }

    void onTick(float dt) override
    {
        GameRuntimeFrameOrchestrator::iterate(app, dt, /*bPumpNativeEvents=*/false);
    }

    void onShutdown() override {}

    [[nodiscard]] bool shouldClose() const override
    {
        return !app.isRunning();
    }

  private:
    App& app;
};
}

App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;

App* App::get()
{
    return _instance;
}

uint32_t App::currentFrameIndex()
{
    return _frameIndex;
}

uint32_t App::getFrameIndex() const
{
    return _frameIndex;
}

namespace
{
ClearValue s_colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue s_depthClearValue = ClearValue(1.0f, 0);
} // namespace

ClearValue& getColorClearValue()
{
    return s_colorClearValue;
}

ClearValue& getDepthClearValue()
{
    return s_depthClearValue;
}

App::App()
    : _renderState(std::make_unique<AppRenderState>())
    , _renderServices(_renderState.get())
    , _sceneServices(this)
    , _automationControlService(std::make_unique<AppAutomationControlService>())
    , _gameUIHost(std::make_unique<GameUIHost>())
    , gameInputNode(inputManager)
{
    inputRouter.setApp(*this);
    inputRouter.setDefaultNode(gameInputNode);
    // Register the render-runtime host services contract once; framework
    // modules (Render3D) consume it through RuntimeServices, never through
    // App globals.
    RuntimeServices::setRenderRuntimeHost(this);
}

App::~App() = default;

int App::run()
{
    // AppKernel is the only native while-loop. The product automation layer
    // still owns scene-stability / screenshot / RenderDoc completion and
    // therefore requests App::requestQuit() itself; do not arm the kernel's
    // basic exit-after-frame policy in parallel during this transition.
    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    HostSdlEventSource      eventSource;
    GameRuntimeLoopDelegate delegate(*this);
    AppKernel               kernel({.eventSource = &eventSource}, delegate);
    return kernel.run();
}

void App::addModule(std::unique_ptr<IModule> module)
{
    YA_CORE_ASSERT(module, "Cannot register a null module");
    YA_CORE_ASSERT(!_modulesAttached, "Modules must be registered before App::init");
    IModule* instance = module.get();
    _modules.push_back({.owned = std::move(module), .module = instance});
}

void App::addModule(IModule& module)
{
    YA_CORE_ASSERT(!_modulesAttached, "Modules must be registered before App::init");
    _modules.push_back({.module = &module});
}

void App::configureModules()
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onConfigure(*this, _ci);
    }
}

void App::applyProjectDescriptor(const FProjectDescriptor& descriptor)
{
    YA_PROFILE_FUNCTION();

    _ci.projectPath      = descriptor.sourcePath.string();
    _ci.projectRoot      = descriptor.sourcePath.parent_path().string();
    _ci.defaultScenePath = descriptor.defaultScene;
    inputManager.configureActionBindings(descriptor.inputActions);

    if (_ci.projectRoot) {
        auto* vfs = VirtualFileSystem::get();
        YA_CORE_ASSERT(vfs != nullptr, "VirtualFileSystem must be initialized before project mounts");
        const auto projectRoot = std::filesystem::path(*_ci.projectRoot);
        vfs->mount(kGameRootMount, projectRoot);
        vfs->mount(kContentMount, projectRoot / "Content");
    }
}

void App::attachModules()
{
    YA_PROFILE_FUNCTION();

    YA_CORE_ASSERT(!_modulesAttached, "Modules are already attached");
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onAttach(*this);
    }
    _modulesAttached = true;
}

void App::detachModules()
{
    YA_PROFILE_FUNCTION();

    if (!_modulesAttached) {
        return;
    }
    for (auto it = _modules.rbegin(); it != _modules.rend(); ++it) {
        getRuntimeModule(it->module)->onDetach(*this);
    }
    _modulesAttached = false;
}

bool App::dispatchModuleEvent(const Event& event)
{
    for (const auto& slot : _modules) {
        if (getRuntimeModule(slot.module)->onEvent(*this, event)) {
            return true;
        }
    }
    return false;
}

bool App::dispatchInputFallbackEvent(const Event& event)
{
    if (event.getEventType() == EEvent::KeyReleased) {
        const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
        if (keyEvent.getKeyCode() == EKey::Escape) {
            requestQuit();
            return true;
        }
    }

    if (event.getEventType() == EEvent::MouseButtonReleased && _appMode == AppMode::Drawing) {
        const auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (mouseEvent.GetMouseButton() == EMouse::Left) {
            clicked.push_back(_lastMousePos);
            return true;
        }
    }

    // Game-UI picking lives in the input node chain (GameInputNode /
    // EditorInputNode) so that an exclusive UI hit can keep the event away
    // from gameplay.
    return false;
}

void App::setInputMode(EInputMode mode)
{
    _inputModeStack.clear();
    _inputMode = mode;
    inputRouter.applyInputMode(mode);
}

void App::pushInputMode(EInputMode mode)
{
    _inputModeStack.push_back(_inputMode);
    _inputMode = mode;
    inputRouter.applyInputMode(mode);
}

void App::popInputMode()
{
    if (_inputModeStack.empty()) {
        YA_CORE_WARN("App::popInputMode: mode stack is empty");
        return;
    }
    _inputMode = _inputModeStack.back();
    _inputModeStack.pop_back();
    inputRouter.applyInputMode(_inputMode);
}

EWidgetRouteResult App::dispatchUIInputEvent(const Event& event)
{
    if (isStopped() || _inputMode == EInputMode::GameOnly) {
        return EWidgetRouteResult::NotHandled;
    }

    const EEvent::T eventType = event.getEventType();
    if (eventType != EEvent::MouseButtonPressed &&
        eventType != EEvent::MouseButtonReleased &&
        eventType != EEvent::MouseMoved) {
        return EWidgetRouteResult::NotHandled;
    }

    // While the game holds the mouse (relative capture) it owns all input:
    // game-UI picking is suspended, matching the editor layout lock.
    if (inputRouter.isMouseCaptured()) {
        return EWidgetRouteResult::NotHandled;
    }

    // Game UI input routes through the GameUIHost's WidgetTree (the single
    // live UI fact source); the scene tree no longer participates in picking.
    GameUIHost* gameUIHost = getGameUIHost();
    if (!gameUIHost || !gameUIHost->getMountedScene()) {
        return EWidgetRouteResult::NotHandled;
    }

    switch (gameUIHost->dispatchEvent(event, _lastMousePos)) {
    case EWidgetRouteResult::HandledExclusive:
        return EWidgetRouteResult::HandledExclusive;
    case EWidgetRouteResult::HandledPass:
        return EWidgetRouteResult::HandledPass;
    case EWidgetRouteResult::NotHandled:
    default:
        return EWidgetRouteResult::NotHandled;
    }
}

void App::tickModules(float dt)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onLogic(*this, dt);
    }
}

void App::prepareModulesForRender(float dt)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onBeforeRender(*this, dt);
    }
}

void App::recordModuleViewportCompose(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onViewportCompose(*this, commandBuffer, dt);
    }
}

void App::recordModuleBeforePresentation(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onBeforePresentation(*this, commandBuffer, dt);
    }
}

void App::recordModulePresentation(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onPresentation(*this, commandBuffer, dt);
    }
}

bool App::notifyModulesBeforeAppStateChange(AppState nextState)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        if (!getRuntimeModule(slot.module)->onBeforeAppStateChange(*this, _appState, nextState)) {
            return false;
        }
    }
    return true;
}

void App::notifyModulesAfterAppStateChange(AppState previousState)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onAfterAppStateChange(*this, previousState, _appState);
    }
}

void App::notifyModulesSceneActivated(Scene* scene)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onSceneActivated(*this, scene);
    }
}

void App::notifyModulesSceneDestroyed(Scene* scene)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onSceneDestroyed(*this, scene);
    }
}


INativeWindow* App::getMainNativeWindow()
{
    return _nativeWindowManager ? _nativeWindowManager->getMainWindow() : nullptr;
}

INativeWindow* App::getOrCreateMainNativeWindow(const WindowCreateInfo& ci)
{
    if (!_nativeWindowManager) {
        return nullptr;
    }
    if (auto* window = _nativeWindowManager->getMainWindow()) {
        return window;
    }
    return _nativeWindowManager->createMainWindow(ci);
}

ShadowSettings* App::getShadowSettings()
{
    return &getRenderServices().getShadowSettings();
}

const AppAutomationShadowOverrides* App::getAutomationShadowOverrides() const
{
    return &_ci.automation.shadow;
}


OffscreenJobQueueService App::getOffscreenJobQueueService()
{
    return OffscreenJobQueueService{
        .enqueue = [this](const std::shared_ptr<OffscreenJobState>& job, std::function<void(ICommandBuffer*)> task)
        {
            taskManager.enqueueOffscreenTask(job, std::move(task));
        },
    };
}

GameplayResourceBinding* App::getGameplayResourceBinding() const
{
    auto* runtime = getRenderServices().getRenderRuntime();
    return runtime ? runtime->getGameplayResourceBinding() : nullptr;
}

EnvironmentLightingProcessor* App::getEnvironmentLightingProcessor() const
{
    auto* runtime = getRenderServices().getRenderRuntime();
    return runtime ? runtime->getEnvironmentLightingProcessor() : nullptr;
}

TerrainProcessor* App::getTerrainProcessor() const
{
    auto* runtime = getRenderServices().getRenderRuntime();
    return runtime ? runtime->getTerrainProcessor() : nullptr;
}
uint64_t App::getElapsedTimeMS() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - _startTime).count();
}

void* App::queryModuleInterface(FInterfaceId interfaceId) const
{
    for (const auto& slot : _modules) {
        if (void* iface = slot.module->queryInterface(interfaceId)) {
            return iface;
        }
    }
    return nullptr;
}

bool App::openProject(const FProjectDescriptor& descriptor)
{
    YA_PROFILE_FUNCTION();

    applyProjectDescriptor(descriptor);

    if (descriptor.defaultScene && !descriptor.defaultScene->empty()) {
        return _sceneServices.loadScene(*descriptor.defaultScene);
    }

    return true;
}

} // namespace ya
