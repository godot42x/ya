#pragma once
#include "Core/Base.h"
#include "Core/Camera/Camera.h"
#include "Core/Camera/FreeCameraController.h"
#include "Core/Camera/OrbitCameraController.h"
#include "Core/Input/InputManager.h"
#include "Core/System/System.h"

#include "Editor/EditorLayer.h"

#include "Runtime/App/FPSCtrl.h"

#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"
#include "Render/Shader.h"
#include "Scene/SceneManager.h"
#include <array>
#include <glm/glm.hpp>



#include "ClLIParams.h"

// Forward declarations
namespace ya
{
struct SceneManager;
struct Scene;
struct Material;
struct IRenderPass;
struct LuaScriptingSystem;
struct Texture;
struct Sampler;
struct RenderDocCapture;
struct ForwardRenderPipeline;
struct DeferredRenderPipeline;


void imcFpsControl(FPSControl& fpsCtrl);
bool imcEditorCamera(FreeCamera& camera);
void imcClearValues();

enum AppMode
{
    Control,
    Drawing,
};

/**
 * @brief Application runtime state
 *
 * Editor: Editing mode, no gameplay simulation
 * Simulation: Play mode in editor, can pause/step
 * Runtime: Standalone game mode
 */
enum class AppState
{
    Editor,     // Editing scene, no gameplay
    Simulation, // Playing in editor (can pause/step)
    Runtime     // Standalone runtime (game build)
};

// enum class SimulationState
// {
//     Stopped,
//     Playing,
//     Paused,
//     Stepping // Advance one frame
// };


struct AppDesc
{
    CliParams params = CliParams("Yet Another Game Engine", "Command line options");

    std::string title      = "Yet Another Game Engine";
    int         width      = 1024;
    int         height     = 768;
    bool        fullscreen = false;
    std::string defaultScenePath;

    bool                     bEnableRenderDoc          = false;
    std::string              renderDocDllPath          = "C:/Program Files/RenderDoc/renderdoc.dll";
    std::string              renderDocCaptureOutputDir = "Engine/Saved/RenderDoc";
    std::vector<std::string> disabledGraphicsCards; // e.g. ["NVIDIA GeForce RTX 3060"]



    void init(int argc, char** argv)
    {
        YA_CORE_INFO(FUNCTION_SIG);
        params
            .opt<int>("w", {"width"}, "Window width")
            .opt<int>("h", {"height"}, "Window height")
            .opt<bool>("f", {"fullscreen"}, "Fullscreen mode", "false")
            .opt<std::string>("", {"renderdoc-dll"}, "RenderDoc dll path", renderDocDllPath)
            .opt<std::string>("", {"renderdoc-output"}, "RenderDoc capture output directory", renderDocCaptureOutputDir)
            .parse(argc, argv);

        title = params._opt.program();
        params.tryGet<int>("width", width);
        params.tryGet<int>("height", height);
        params.tryGet<bool>("fullscreen", fullscreen);
        params.tryGet<std::string>("renderdoc-dll", renderDocDllPath);
        params.tryGet<std::string>("renderdoc-output", renderDocCaptureOutputDir);
    }
};

struct TaskManager
{
    std::queue<std::function<void()>> tasks;

    void registerFrameTask(std::function<void()> task)
    {
        tasks.push(std::move(task));
    }

    void update()
    {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop();
            task();
        }
    }
};



struct App
{
    static App* _instance;

    Deleter _deleter;

    // Core subsystems
    IRender*      _render       = nullptr;
    SceneManager* _sceneManager = nullptr;

    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;

    std::shared_ptr<ShaderStorage>                                   _shaderStorage = nullptr;
    std::vector<std::pair<std::string /*name*/, IGraphicsPipeline*>> _monitorPipelines;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    // Runtime state
    bool          bRunning         = true;
    ERenderAPI::T currentRenderAPI = ERenderAPI::None;

    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;

    static uint32_t _frameIndex;
    bool            _bPause     = false;
    bool            _bMinimized = false; // Track window minimized state

    AppDesc   _ci;
    glm::vec2 _windowSize = {0, 0};

    // State management
    AppState _appState = AppState::Editor;
    // SimulationState _simulationState = SimulationState::Stopped;

    // Input and Camera
    InputManager inputManager;
    TaskManager  taskManager;

    FreeCamera            camera;
    FreeCameraController  cameraController;
    OrbitCameraController orbitCameraController;

    // Application state
    AppMode   _appMode      = AppMode::Control;
    glm::vec2 _lastMousePos = {0, 0};

    static const auto COLOR_FORMAT = EFormat::R8G8B8A8_UNORM;
    static const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;

    // Render targets (simplified - only manage attachments, no RenderPass dependency)
    Rect2D                 _viewportRect;
    float                  _viewportFrameBufferScale = 1.0f;
    std::vector<glm::vec2> clicked;

    std::shared_ptr<IRenderPass>   _screenRenderPass = nullptr; // Legacy render pass (unused in dynamic rendering)
    std::shared_ptr<IRenderTarget> _screenRT         = nullptr; // Swapchain RT for ImGui

    // other systems, eg: transform, resource resolve
    std::vector<stdptr<ISystem>> _systems;

    bool bRenderMirror = false;


    EditorLayer* _editorLayer;

    MulticastDelegate<void()> onScenePostInit;

    LuaScriptingSystem*      _luaScriptingSystem;
    stdptr<RenderDocCapture> _renderDocCapture;
    int                      _renderDocOnCaptureAction = 0; // 0: none, 1: open replay UI, 2: open capture folder
    std::string              _renderDocLastCapturePath;
    std::string              _renderDocConfiguredDllPath;
    std::string              _renderDocConfiguredOutputDir;


  public:
    App()                      = default;
    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App(App&&)                 = delete;
    App& operator=(App&&)      = delete;
    virtual ~App()             = default;

    void init(AppDesc ci);
    int  run();
    int  iterate(float dt);
    void quit();
    int  processEvent(SDL_Event& event);

    template <typename T>
    int  dispatchEvent(const T& event);
    void renderGUI(float dt);

    void requestQuit()
    {
        stopRuntime();
        bRunning = false;
    }

    // before render context created
    virtual void onInit(AppDesc ci);
    //  after render context created
    virtual void onPostInit();
    virtual void onQuit() {}


    virtual int  onEvent(const Event& event);
    virtual void tickLogic(float dt);
    virtual void tickRender(float dt);
    virtual void onRenderGUI(float dt);



    static App* get() { return _instance; }


    [[nodiscard]] IRender* getRender() const { return _render; }
    template <typename T>
    [[nodiscard]] T* getRender() { return static_cast<T*>(getRender()); }

    [[nodiscard]] const AppDesc&                 getDesc() const { return _ci; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }

    [[nodiscard]] ForwardRenderPipeline* getForwardPipeline() const;
    [[nodiscard]] bool                   isShadowMappingEnabled() const;
    [[nodiscard]] bool                   isMirrorRenderingEnabled() const;
    [[nodiscard]] bool                   hasMirrorRenderResult() const;
    [[nodiscard]] IRenderTarget*         getShadowDepthRT() const;
    [[nodiscard]] IImageView*            getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*            getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] Texture*               getPostprocessOutputTexture() const;
    [[nodiscard]] IRenderTarget*         getMirrorRenderTarget() const;
    [[nodiscard]] bool                   isPostprocessingEnabled() const;

    // Getters for subsystems
    [[nodiscard]] SceneManager* getSceneManager() const { return _sceneManager; }
    [[nodiscard]] uint32_t      getFrameIndex() const { return _frameIndex; }
    [[nodiscard]] uint64_t      getElapsedTimeMS() const { return std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - _startTime).count(); }

    // State getters
    [[nodiscard]] AppState getAppState() const { return _appState; }
    // [[nodiscard]] SimulationState getSimulationState() const { return _simulationState; }
    [[nodiscard]] bool isEditorMode() const { return _appState == AppState::Editor; }
    [[nodiscard]] bool isSimulationMode() const { return _appState == AppState::Simulation; }
    [[nodiscard]] bool isRuntimeMode() const { return _appState == AppState::Runtime; }
    bool               isPaused() const { return _bPause; }
    // [[nodiscard]] bool            isSimulationPlaying() const { return _simulationState == SimulationState::Playing; }
    // [[nodiscard]] bool            isSimulationPaused() const { return _simulationState == SimulationState::Paused; }
    // [[nodiscard]] float getSimulationDeltaTime() const { return _simulationDeltaTime; }
    // [[nodiscard]] float getEditorDeltaTime() const { return _editorDeltaTime; }

    void startRuntime();
    void startSimulation();
    void stopRuntime();
    void stopSimulation();

    // Get primary camera from ECS (entity with CameraComponent._primary == true)
    // Returns nullptr if no primary camera found
    Entity* getPrimaryCamera() const;

    bool loadScene(const std::string& path);
    bool unloadScene();

    glm::vec2 getLastMousePos() const { return _lastMousePos; }

  protected:
    // Protected for derived classes to override
    virtual void onSceneInit(Scene* scene);
    virtual void onSceneDestroy(Scene* scene);
    virtual void onSceneActivated(Scene* scene);

    // void onScenePostInit(Scene *scene) {}

    virtual void onEnterRuntime();
    virtual void onEnterSimulation() {}
    virtual void onExitSimulation() {}
    virtual void onSimulationPaused() {}
    virtual void onSimulationResumed() {}

  private:
    // temp
    [[nodiscard]] const InputManager& getInputManager() const { return inputManager; }
    [[nodiscard]] const glm::vec2&    getWindowSize() const { return _windowSize; }


  private:


    bool saveScene([[maybe_unused]] const std::string& path) { return false; } // TODO: implement

    bool onWindowResized(const WindowResizeEvent& event);
    bool onKeyReleased(const KeyReleasedEvent& event);
    bool onMouseMoved(const MouseMoveEvent& event);
    bool onMouseButtonReleased(const MouseButtonReleasedEvent& event);
    bool onMouseScrolled(const MouseScrolledEvent& event);
    void onSceneViewportResized(Rect2D rect);


    void handleSystemSignals();
    bool recreateViewPortRT(uint32_t width, uint32_t height);

    void initRenderPipeline();
    void shutdownRenderPipeline();
    void tickRenderPipeline(float dt);
    void renderGUIRenderPipeline();
};


} // namespace ya