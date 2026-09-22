#pragma once

#include "Core/Api.h"
#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/InputMode.h"
#include "GameRuntime/InputRouter.h"
#include "Core/MessageBus.h"
#include "App/Module/Module.h"
#include "Core/System/System.h"
#include "GameRuntime/AppOptions.h"
#include "GameRuntime/AppRenderServices.h"
#include "Render3D/Common/RenderRuntimeHostServices.h"
#include "Render3D/Common/FrameRecordExtensions.h"
#include "GameRuntime/AppSceneServices.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "Core/Common/AppState.h"
#include "GameRuntime/AppTaskManager.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "GameRuntime/Render/RuntimeGameViewProducer.h"

#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// Default clear values. Exposed through accessor functions rather than extern
// data symbols: a dllexport data symbol cannot be imported from another DLL
// (the module export macro propagates into every consumer), whereas function
// symbols resolve cleanly through the import library.
YA_GAME_RUNTIME_API ClearValue& getColorClearValue();
YA_GAME_RUNTIME_API ClearValue& getDepthClearValue();

struct Scene;
enum class EWidgetRouteResult : uint8_t;
struct SceneManager;
struct UIDocumentStore;
struct Entity;
struct ICommandBuffer;
struct LuaScriptingSystem;
struct JSScriptingSystem;
struct FProjectDescriptor;
struct AppRenderState;
class NativeWindowManager;
struct GameplayResourceBinding;
struct EnvironmentLightingProcessor;
class TerrainProcessor;
class GameRuntimeTickOrchestrator;
class AppModuleTestAccess;
class AppAutomationControlService;
class InputRouter;

struct YA_GAME_RUNTIME_API App : public IRenderRuntimeHostServices,
                                 public IFrameRecordExtensions
{
    friend class GameRuntimeTickOrchestrator;
    friend class AppModuleTestAccess;
    friend class AppSceneServices;
    friend class InputRouter;

  private:
    static App* _instance;

    /// Set by quit(). App teardown is one ordered path: the implicit member
    /// destruction order is not a valid order for a live device plus the native
    /// windows it created surfaces from, and the teardown stack has to run while
    /// those members are still alive.
    bool _bTornDown = false;

    Deleter _deleter;

    SceneManager*                   _sceneManager = nullptr;
    std::unique_ptr<NativeWindowManager> _nativeWindowManager;
    std::unique_ptr<AppRenderState> _renderState;
    /// The game's world viewport declaration (see RuntimeGameViewProducer).
    RuntimeGameViewProducer                      _gameViewProducer;
    AppRenderServices                            _renderServices;
    AppSceneServices                             _sceneServices;
    std::unique_ptr<AppAutomationControlService> _automationControlService;
    /// Game UI presentation adapter: owns the live WidgetTree of the current
    /// game presentation area and resolves scenes/input/frames to it.
    std::unique_ptr<GameUIHost> _gameUIHost;
    /// Game UI document table (SceneWidgetEntry::documentPath -> live
    /// UIDocument). The app owns it so the editor designer, the inspector and
    /// the runtime host resolve the same instance.
    std::unique_ptr<UIDocumentStore> _uiDocuments;

    bool bRunning = true;

    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;

    static uint32_t _hostTick;
    bool            _bPause     = false;
    /// Main present surface unpresentable (minimize). Not a process pause:
    /// logic still ticks; coordinator skips GPU when `begin` returns -1.
    bool            _bMinimized = false;

    AppDesc   _ci;
    AppState  _appState   = AppState::Stopped;

    InputManager inputManager;
    GameInputNode gameInputNode;
    InputRouter  inputRouter;
    TaskManager  taskManager;

    EInputMode                _inputMode = EInputMode::GameAndUI;
    std::vector<EInputMode>   _inputModeStack;
    glm::vec2 _lastMousePos = {0, 0};

    std::vector<stdptr<ISystem>> _systems;

    struct FModuleSlot
    {
        std::unique_ptr<IModule> owned;
        IModule*                 module = nullptr;
    };

    std::vector<FModuleSlot> _modules;
    bool                     _modulesAttached = false;

    LuaScriptingSystem* _luaScriptingSystem = nullptr;
    JSScriptingSystem*  _jsScriptingSystem  = nullptr;

  public:
    App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App(App&&)                 = delete;
    App& operator=(App&&)      = delete;
    /// Destruction runs the same teardown as quit(): an App that entered init()
    /// owns a device, native windows and a teardown stack, and letting unord-
    /// ered member destruction handle those is what turns a failed startup into
    /// a crash instead of an exit code.
    virtual ~App();

    void init(AppDesc ci);
    int  run();
    void quit();

    template <typename T>
    int dispatchEvent(const T& event)
    {
        if (0 == onEvent(event)) {
            MessageBus::get()->publish(event);
        }
        return 0;
    }
    /// Runtime-typed bridge used by AppKernel event sources. SDL/script event
    /// pumps erase the concrete C++ event type, so publish through the event's
    /// dynamic type rather than instantiating MessageBus::publish<Event>.
    int dispatchEvent(const Event& event)
    {
        if (0 == onEvent(event)) {
            MessageBus::get()->publishEvent(event);
        }
        return 0;
    }

    void addModule(std::unique_ptr<IModule> module);
    void addModule(IModule& module);

    /// Register/unregister a view owner. Producers are owned by whoever shows
    /// the viewport (the runtime owns the game view, a module owns its own), so
    /// the App only keeps the list it collects from each tick.
    void addSceneViewProducer(ISceneViewProducer& producer);
    void removeSceneViewProducer(ISceneViewProducer& producer);

    void requestQuit()
    {
        if (AppAutomation::shouldDeferQuit(*this)) {
            return;
        }
        if (isRuntimeMode()) {
            stopRuntime();
        }
        else if (isSimulationMode()) {
            stopSimulation();
        }
        bRunning = false;
    }

    virtual void onInit(const AppDesc& ci);
    virtual void onPostInit();
    virtual void onQuit() {}

    virtual int  onEvent(const Event& event);

    // Defined in App.cpp. Kept out of the header so consumers resolve the
    // singleton through the DLL import thunk instead of inlining a direct
    // read of the static data members `_instance`/`_hostTick` (a dllexport
    // data symbol cannot be imported from another DLL and would fail LNK2001).
    static App* get();
    [[nodiscard]] static uint32_t currentHostTick();

    [[nodiscard]] AppRenderServices&       getRenderServices() { return _renderServices; }
    [[nodiscard]] const AppRenderServices& getRenderServices() const { return _renderServices; }
    [[nodiscard]] AppSceneServices&        getSceneServices() { return _sceneServices; }
    [[nodiscard]] const AppSceneServices&  getSceneServices() const { return _sceneServices; }
    [[nodiscard]] GameUIHost*                        getGameUIHost() { return _gameUIHost.get(); }
    [[nodiscard]] UIDocumentStore*                   getUIDocumentStore() { return _uiDocuments.get(); }
    [[nodiscard]] JSScriptingSystem*                  getJSScriptingSystem() const { return _jsScriptingSystem; }

    [[nodiscard]] const AppDesc&                 getDesc() const { return _ci; }
    [[nodiscard]] EnvironmentLightingProcessor*  getEnvironmentLightingProcessor() const;
    [[nodiscard]] TerrainProcessor*              getTerrainProcessor() const;

    // === IRenderRuntimeHostServices implementation ===
    INativeWindow* getOrCreateMainNativeWindow(const WindowCreateInfo& ci) override;
    ShadowSettings*                        getShadowSettings() override;
    const AppAutomationShadowOverrides*  getAutomationShadowOverrides() const override;
    OffscreenJobQueueService getOffscreenJobQueueService() override;
    [[nodiscard]] InputManager&                  getInputManager() { return inputManager; }
    [[nodiscard]] const InputManager&            getInputManager() const { return inputManager; }
    [[nodiscard]] InputRouter&                   getInputRouter() { return inputRouter; }
    [[nodiscard]] const InputRouter&             getInputRouter() const { return inputRouter; }
    [[nodiscard]] TaskManager&                   getTaskManager() { return taskManager; }
    [[nodiscard]] const TaskManager&             getTaskManager() const { return taskManager; }

    // Defined in App.cpp (see the data-symbol note on get()/currentHostTick()).
    [[nodiscard]] uint32_t                getHostTick() const;
    [[nodiscard]] uint64_t                getElapsedTimeMS() const;

    [[nodiscard]] AppState getAppState() const { return _appState; }
    [[nodiscard]] bool     isRunning() const { return bRunning; }
    [[nodiscard]] bool     isStopped() const override { return _appState == AppState::Stopped; }
    [[nodiscard]] bool     isSimulationMode() const { return _appState == AppState::Simulation; }
    [[nodiscard]] bool     isRuntimeMode() const { return _appState == AppState::Runtime; }

    /// Broadcast after every app mode transition (Runtime / Simulation / Stopped).
    MulticastDelegate<void(AppState)> onAppStateChanged;
    bool                   isPaused() const { return _bPause; }

    // === Input mode (game / UI routing + cursor baseline) ===
    [[nodiscard]] EInputMode getInputMode() const { return _inputMode; }
    /// Hard-set the input mode and clear the mode stack.
    void setInputMode(EInputMode mode);
    /// Push a mode (e.g. open a pause menu with UIOnly); pop restores the
    /// previous mode, so "forgot to restore" bugs cannot happen.
    void pushInputMode(EInputMode mode);
    void popInputMode();
    /// Dispatch the event to the Game UI WidgetTree (GameUIHost) under the
    /// current mode. Returns NotHandled when UI is disabled / nothing hit.
    [[nodiscard]] EWidgetRouteResult dispatchUIInputEvent(const Event& event);

    void startRuntime();
    void startSimulation();
    void stopRuntime();
    void stopSimulation();
    bool openProject(const FProjectDescriptor& descriptor);

    [[nodiscard]] void* queryModuleInterface(FInterfaceId interfaceId) const;
    template <typename T>
    [[nodiscard]] T* queryModuleInterface(FInterfaceId interfaceId) const
    {
        return static_cast<T*>(queryModuleInterface(interfaceId));
    }

    /// What the primary surface shows before the host's own display passes: a
    /// View's display image (true), or nothing but the pass clear (false)
    /// because a module fills the surface itself.
    ///
    /// Not a question about AppState. The editor's Play runs the game in
    /// Runtime mode and still draws its chrome across the whole window, so
    /// "runtime" does not mean "the window is the game's view". The module that
    /// owns the surface is what decides; see
    /// `IRuntimeModule::fillsPrimarySurface`.
    [[nodiscard]] bool presentsViewDisplayImage() const;

    glm::vec2 getLastMousePos() const { return _lastMousePos; }

  protected:
    virtual void onEnterRuntime();
    virtual void onEnterSimulation() {}
    virtual void onExitSimulation() {}
    virtual void onSimulationPaused() {}
    virtual void onSimulationResumed() {}

  private:
    void handleSystemSignals();
    [[nodiscard]] static std::string resolveStartupScenePath(const AppDesc& appDesc);
    [[nodiscard]] bool loadSceneInternal(const std::string& path);
    [[nodiscard]] bool unloadSceneInternal();
    void handleSceneInit(Scene* scene);
    void handleSceneDestroy(Scene* scene);
    void handleSceneActivated(Scene* scene);
    bool handleWindowResized(const WindowResizeEvent& event);
    void handleMouseMoved(const MouseMoveEvent& event);
    [[nodiscard]] NativeWindowManager* getNativeWindowManager() { return _nativeWindowManager.get(); }
    [[nodiscard]] const NativeWindowManager* getNativeWindowManager() const { return _nativeWindowManager.get(); }
    [[nodiscard]] AppAutomationControlService* getAutomationControlService() { return _automationControlService.get(); }
    [[nodiscard]] const AppAutomationControlService* getAutomationControlService() const { return _automationControlService.get(); }
    void attachModules();
    void detachModules();
    void configureModules();
    void applyProjectDescriptor(const FProjectDescriptor& descriptor);
    [[nodiscard]] bool dispatchModuleEvent(const Event& event);
    [[nodiscard]] bool dispatchInputFallbackEvent(const Event& event);
    void tickModules(float dt);
    void prepareModulesForRender(float dt);
    /// IFrameRecordExtensions: the host's contribution to the renderer's record
    /// order. Each stage loops the attached runtime modules, which is where the
    /// editor records its chrome and overlays; capture is automation's.
    void recordViewCompose(ICommandBuffer& cmdBuf, float deltaTime) override;
    void recordBeforeDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime) override;
    void recordDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime) override;
    [[nodiscard]] bool appendDisplayCapture(RenderGraph& graph,
                                            RGTextureHandle presentationOutput,
                                            Extent2D presentationExtent) override;
    void presentModuleExtras(float dt);
    [[nodiscard]] bool notifyModulesBeforeAppStateChange(AppState nextState);
    void notifyModulesAfterAppStateChange(AppState previousState);
    void notifyModulesSceneActivated(Scene* scene);
    void notifyModulesSceneDestroyed(Scene* scene);
};

} // namespace ya
