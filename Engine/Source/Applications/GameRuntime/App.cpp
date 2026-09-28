#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/IRuntimeModule.h"
#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"
#include "App/Kernel/SdlEventSource.h"
#include "GUI/Host/NativeWindowManager.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "App/Kernel/AppKernel.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Os/InstanceRegistry.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render3D/RenderDeviceState.h"

#include "App/Module/ProjectDescriptor.h"
#include "Core/Profiling/Profiling.h"
#include "Core/System/VirtualFileSystem.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/NativeWindow.h"
#include "Scene/Core/GameMounts.h"
#include "Scene/Core/Scene.h"
#include "Render3D/Services/DebugRenderSystem.h"

#include <format>

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

/// One-instance identity: two runs collide when they share a project and mode
/// (they fight over the automation port and the build outputs, and the loser
/// is silent). Two different projects are a legitimate pair, so the key is the
/// project, not the executable; the mode suffix keeps an editor and a game run
/// on the same project apart.
Os::FInstanceRecord makeInstanceRecord(const AppDesc& desc)
{
    const char* mode = desc.bEditor ? "editor" : "game";
    return Os::FInstanceRecord{
        .key         = std::format("{}|{}",
                                   desc.projectPath.value_or(desc.executablePath.value_or("ya")),
                                   mode),
        .project     = desc.projectPath.value_or(""),
        .mode        = mode,
        .controlPort = desc.automation.controlPort,
    };
}

class GameRuntimeLoopDelegate final : public IAppLoopDelegate
{
  public:
    explicit GameRuntimeLoopDelegate(App& inApp)
        : app(inApp)
    {
    }

    void onInit() override {}
    void onEvent(const Event& event) override
    {
        app.dispatchEvent(event);
    }

    void onTick(float dt) override
    {
        GameRuntimeTickOrchestrator::iterate(app, dt);
    }

    void onShutdown() override {}

    [[nodiscard]] bool shouldClose() const override
    {
        return !app.isRunning();
    }

  private:
    App& app;
};

/// The runtime measures the pump phase (trace zone + perf sample, game
/// domain). The window-level protocol itself is the shared SdlEventSource;
/// this adds only the measurement around it.
class InstrumentedEventSource final : public IAppEventSource
{
  public:
    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        YA_PROFILE_SCOPE("Tick/EventPump");
        YA_PERF_SCOPE(perf::sample::tickEventPump(), perf::metric::cpuTimeMs(), perf::domain::game());
        _source.pollEvents(emit);
    }

  private:
    SdlEventSource _source;
};
}

App*     App::_instance        = nullptr;
uint32_t App::App::_hostTick = 0;

App* App::get()
{
    return _instance;
}

uint32_t App::currentHostTick()
{
    return _hostTick;
}

uint32_t App::getHostTick() const
{
    return _hostTick;
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
    , _uiDocuments(std::make_unique<UIDocumentStore>())
    , gameInputNode(inputManager)
{
    inputRouter.setApp(*this);
    inputRouter.setDefaultNode(gameInputNode);
    _gameUIHost->setDocumentStore(_uiDocuments.get());
}

App::~App()
{
    // Only an App that entered init() owns a teardown: unit tests (and tooling)
    // build App for its services and never init it. `_instance` is assigned at
    // the top of init and cleared by quit(), so it reads exactly as "init ran and
    // teardown did not", which is when the ordered teardown below is needed.
    if (App::_instance == this) {
        quit();
    }
}

int App::run()
{
    // AppKernel is the only native while-loop. The product automation layer
    // still owns scene-stability / screenshot / RenderDoc completion and
    // therefore requests App::requestQuit() itself; do not arm the kernel's
    // basic exit-after-frame policy in parallel during this transition.
    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    InstrumentedEventSource eventSource;
    GameRuntimeLoopDelegate delegate(*this);
    AppKernel               kernel({.eventSource = &eventSource,
                                    .instanceRecord = makeInstanceRecord(_ci)},
                                   delegate);
    // The runtime owns frame-level automation completion (scene stability,
    // screenshots, RenderDoc), so exitAfterTick stays off here. The wall-clock
    // deadline is different: it is the one policy that has to hold even when
    // nothing else asks the app to stop.
    return kernel.run(AppAutomationRunOptions{
        .maxLifetimeSeconds = _ci.automation.maxLifetimeSeconds,
    });
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

void App::addSceneViewProducer(ISceneViewProducer& producer)
{
    if (!_renderState) {
        return;
    }
    auto& producers = _renderState->viewProducers;
    if (std::find(producers.begin(), producers.end(), &producer) == producers.end()) {
        // View keys are owner-scoped, and the owner is what makes a key unique:
        // two producers claiming one owner would mint colliding View ids, and
        // the output tables key on that id. Cheap here, impossible to notice
        // later, so it is asserted at the one place every producer passes.
        YA_CORE_ASSERT(producer.viewOwner() != 0,
                       "a scene view producer must name the owner its View keys belong to");
        for (const ISceneViewProducer* other : producers) {
            YA_CORE_ASSERT(other->viewOwner() != producer.viewOwner(),
                           "scene view owner {} is claimed by more than one producer; View keys must not collide",
                           producer.viewOwner());
        }
        producers.push_back(&producer);
        // The producer owns its Views for as long as it is registered, so the
        // target store keeps them across ticks that declare none of them.
        if (RenderDeviceState* device = _renderState->device.get()) {
            for (const uint32_t local : producer.ownedViewLocalIds()) {
                device->registerSceneView(producer.viewKey(local));
            }
        }
    }
}

void App::removeSceneViewProducer(ISceneViewProducer& producer)
{
    if (!_renderState) {
        return;
    }
    // Unregister first: this is the lifecycle boundary that drops the View's
    // allocation and publication, not the erasure of the collector entry.
    if (RenderDeviceState* device = _renderState->device.get()) {
        for (const uint32_t local : producer.ownedViewLocalIds()) {
            device->unregisterSceneView(producer.viewKey(local));
        }
    }
    std::erase(_renderState->viewProducers, &producer);
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

    // Packed games own the Dock / taskbar icon. Editor keeps YA branding.
    if (!_ci.bEditor) {
        if (descriptor.icon) {
            setProcessWindowIconPath(descriptor.resolvePath(*descriptor.icon).string());
        }
        else {
            setProcessWindowIconPath({});
        }
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

    // An explicit resolution is stretched onto the window. Widgets were laid
    // out in that resolution, so a window-space click has to be mapped back
    // before the hit test. FollowWindow keeps the two sizes equal, and Hold
    // (the editor panel) is already in window pixels.
    glm::vec2 presentedPoint = _lastMousePos;
    if (_renderState &&
        _renderState->hostSettings.resolutionPolicy == EHostResolutionPolicy::ExplicitStretch) {
        int winW = 0;
        int winH = 0;
        if (NativeWindowManager* nwm = getNativeWindowManager()) {
            if (INativeWindow* window = nwm->getMainWindow()) {
                window->getWindowSize(winW, winH);
            }
        }
        const Extent2D resolution = _renderState->hostSettings.renderResolution;
        if (winW > 0 && winH > 0 && resolution.width > 0 && resolution.height > 0) {
            presentedPoint.x = _lastMousePos.x * static_cast<float>(resolution.width) / static_cast<float>(winW);
            presentedPoint.y = _lastMousePos.y * static_cast<float>(resolution.height) / static_cast<float>(winH);
        }
    }

    switch (gameUIHost->dispatchEvent(event, presentedPoint)) {
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

bool App::presentsViewDisplayImage(const IRenderSurfaceContext& surface) const
{
    // Asked before display compose records, and answered from module state
    // rather than from a flag a module set earlier in the frame: the renderer
    // gets the same answer the recording below will produce. The loop is the
    // whole policy -- one module filling a surface is enough to make the View
    // copy pointless, and a host with no such module (a standalone runtime)
    // shows the View. There is no surface whose answer stands for the others:
    // each window is asked about itself.
    for (const auto& slot : _modules) {
        if (getRuntimeModule(slot.module)->fillsSurface(surface)) {
            return false;
        }
    }
    return true;
}

void App::recordViewCompose(ICommandBuffer& cmdBuf, float deltaTime)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onViewportCompose(*this, cmdBuf, deltaTime);
    }
}

void App::recordBeforeDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onBeforePresentation(*this, cmdBuf, deltaTime);
    }
}

void App::recordDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->onPresentation(*this, cmdBuf, deltaTime);
    }
}

bool App::appendDisplayCapture(RenderGraph&    graph,
                               RGTextureHandle presentationOutput,
                               Extent2D        presentationExtent)
{
    // Automation's screenshot pass. Both the config-driven capture and the
    // control-service request append one; the graph reads its output when
    // either did.
    bool bAppended = AppAutomation::appendPresentationCapture(getHostTick(),
                                                              graph,
                                                              presentationOutput,
                                                              presentationExtent);
    if (auto* automationControl = getAutomationControlService()) {
        bAppended = automationControl->appendPresentationCapture(getHostTick(),
                                                                graph,
                                                                presentationOutput,
                                                                presentationExtent) ||
                    bAppended;
    }
    return bAppended;
}

void App::recordModuleExtraSurfaces(float dt, FFrameSubmission& submission)
{
    for (const auto& slot : _modules) {
        getRuntimeModule(slot.module)->recordExtraSurfaces(*this, dt, submission);
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


INativeWindow* App::getOrCreateMainNativeWindow(const WindowCreateInfo& ci)
{
    if (!_nativeWindowManager) {
        return nullptr;
    }
    if (auto* window = _nativeWindowManager->getMainWindow()) {
        return window;
    }
    INativeWindow* window = _nativeWindowManager->createMainWindow(ci);
    if (window) {
        applyWindowChrome(*window, mainWindowChromeMode(), ci.bResizable);
    }
    return window;
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

EnvironmentLightingProcessor* App::getEnvironmentLightingProcessor() const
{
    auto* device = getRenderServices().getDeviceState();
    return device ? device->getEnvironmentLightingProcessor() : nullptr;
}

TerrainProcessor* App::getTerrainProcessor() const
{
    auto* device = getRenderServices().getDeviceState();
    return device ? device->getTerrainProcessor() : nullptr;
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
