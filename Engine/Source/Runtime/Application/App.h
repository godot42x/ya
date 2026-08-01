#pragma once

#include "Core/Api.h"
#include "Core/Base.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/InputRouter.h"
#include "Core/MessageBus.h"
#include "Core/Module/Module.h"
#include "Core/System/System.h"
#include "Runtime/Application/AppOptions.h"
#include "Runtime/Application/AppRenderServices.h"
#include "Runtime/Application/AppSceneServices.h"
#include "Runtime/Application/AppState.h"
#include "Runtime/Application/AppTaskManager.h"
#include "Runtime/Application/Lifecycle/AppAutomation.h"

#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

extern ENGINE_API ClearValue colorClearValue;
extern ENGINE_API ClearValue depthClearValue;

struct Scene;
struct SceneManager;
struct Entity;
struct ICommandBuffer;
struct LuaScriptingSystem;
struct FProjectDescriptor;
struct AppRenderState;
class ResourceResolveSystem;
class AppLifecycle;
class AppFrameLoop;
class AppEventRouter;
class AppModuleTestAccess;
class AppAutomationControlService;
class InputRouter;

enum AppMode : int
{
    Control,
    Drawing,
};

struct ENGINE_API App
{
    friend class AppLifecycle;
    friend class AppFrameLoop;
    friend class AppEventRouter;
    friend class AppModuleTestAccess;
    friend class AppSceneServices;
    friend class InputRouter;

  private:
    static App* _instance;

    Deleter _deleter;

    SceneManager*                   _sceneManager = nullptr;
    std::unique_ptr<AppRenderState> _renderState;
    AppRenderServices                            _renderServices;
    AppSceneServices                             _sceneServices;
    std::unique_ptr<AppAutomationControlService> _automationControlService;

    bool bRunning = true;

    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;

    static uint32_t _frameIndex;
    bool            _bPause     = false;
    bool            _bMinimized = false;

    AppDesc   _ci;
    glm::vec2 _windowSize = {0, 0};
    AppState  _appState   = AppState::Stopped;

    InputManager inputManager;
    GameInputNode gameInputNode;
    InputRouter  inputRouter;
    TaskManager  taskManager;

    AppMode   _appMode      = AppMode::Control;
    glm::vec2 _lastMousePos = {0, 0};

    std::vector<glm::vec2>       clicked;
    std::vector<stdptr<ISystem>> _systems;
    ResourceResolveSystem*       _resourceResolveSystem = nullptr;

    struct FModuleSlot
    {
        std::unique_ptr<IModule> owned;
        IModule*                 module = nullptr;
    };

    std::vector<FModuleSlot> _modules;
    bool                     _modulesAttached = false;

    LuaScriptingSystem* _luaScriptingSystem = nullptr;

  public:
    App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App(App&&)                 = delete;
    App& operator=(App&&)      = delete;
    virtual ~App();

    void init(AppDesc ci);
    int  run();
    int  iterate(float dt);
    void quit();

    template <typename T>
    int dispatchEvent(const T& event)
    {
        if (0 == onEvent(event)) {
            MessageBus::get()->publish(event);
        }
        return 0;
    }

    void addModule(std::unique_ptr<IModule> module);
    void addModule(IModule& module);

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
    virtual void tickLogic(float dt);
    virtual void tickRender(float dt);

    static App* get() { return _instance; }
    [[nodiscard]] static uint32_t currentFrameIndex() { return _frameIndex; }

    [[nodiscard]] AppRenderServices&       getRenderServices() { return _renderServices; }
    [[nodiscard]] const AppRenderServices& getRenderServices() const { return _renderServices; }
    [[nodiscard]] AppSceneServices&        getSceneServices() { return _sceneServices; }
    [[nodiscard]] const AppSceneServices&  getSceneServices() const { return _sceneServices; }
    [[nodiscard]] AppAutomationControlService*       getAutomationControlService() { return _automationControlService.get(); }
    [[nodiscard]] const AppAutomationControlService* getAutomationControlService() const { return _automationControlService.get(); }

    [[nodiscard]] const AppDesc&                 getDesc() const { return _ci; }
    [[nodiscard]] ResourceResolveSystem*         getResourceResolveSystem() const { return _resourceResolveSystem; }
    [[nodiscard]] InputManager&                  getInputManager() { return inputManager; }
    [[nodiscard]] const InputManager&            getInputManager() const { return inputManager; }
    [[nodiscard]] InputRouter&                   getInputRouter() { return inputRouter; }
    [[nodiscard]] const InputRouter&             getInputRouter() const { return inputRouter; }
    [[nodiscard]] TaskManager&                   getTaskManager() { return taskManager; }
    [[nodiscard]] const TaskManager&             getTaskManager() const { return taskManager; }

    [[nodiscard]] uint32_t                getFrameIndex() const { return _frameIndex; }
    [[nodiscard]] uint64_t                getElapsedTimeMS() const;

    [[nodiscard]] AppState getAppState() const { return _appState; }
    [[nodiscard]] bool     isStopped() const { return _appState == AppState::Stopped; }
    [[nodiscard]] bool     isSimulationMode() const { return _appState == AppState::Simulation; }
    [[nodiscard]] bool     isRuntimeMode() const { return _appState == AppState::Runtime; }
    bool                   isPaused() const { return _bPause; }

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

    glm::vec2 getLastMousePos() const { return _lastMousePos; }

  protected:
    virtual void onEnterRuntime();
    virtual void onEnterSimulation() {}
    virtual void onExitSimulation() {}
    virtual void onSimulationPaused() {}
    virtual void onSimulationResumed() {}

  private:
    void attachModules();
    void detachModules();
    void configureModules();
    void applyProjectDescriptor(const FProjectDescriptor& descriptor);
    [[nodiscard]] bool dispatchHostModuleEvent(const Event& event);
    [[nodiscard]] bool dispatchInputModuleEvent(const Event& event);
    [[nodiscard]] bool dispatchInputFallbackEvent(const Event& event);
    void tickModules(float dt);
    void prepareModulesForRender(float dt);
    void recordModuleBeforePresentation(ICommandBuffer& commandBuffer, float dt);
    void recordModulePresentation(ICommandBuffer& commandBuffer, float dt);
    [[nodiscard]] bool notifyModulesBeforeAppStateChange(AppState nextState);
    void notifyModulesAfterAppStateChange(AppState previousState);
    void notifyModulesSceneActivated(Scene* scene);
    void notifyModulesSceneDestroyed(Scene* scene);
    [[nodiscard]] const glm::vec2& getWindowSize() const { return _windowSize; }
    void                              syncViewportState();
    [[nodiscard]] Extent2D            resolveViewportExtent(RenderRuntime* renderRuntime,
                                                            const Rect2D& viewportRect) const;
    void                              prepareRenderFrameState(float dt);
};

} // namespace ya

