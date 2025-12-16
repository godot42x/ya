#pragma once
#include "Core/Base.h"
#include "Core/Camera.h"
#include "Core/Input/InputManager.h"
#include "Core/MessageBus.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"
#include "Render/Shader.h"
#include <glm/glm.hpp>



#include "ClLIParams.h"

// Forward declarations
namespace ya
{
struct SceneManager;
struct ImGuiManager;
struct Scene;
struct Material;
struct IRenderPass;
} // namespace ya


namespace ya
{

enum AppMode
{
    Control,
    Drawing,
};


struct AppDesc
{
    CliParams params = CliParams("Yet Another Game Engine", "Command line options");

    std::string title      = "Yet Another Game Engine";
    int         width      = 1024;
    int         height     = 768;
    bool        fullscreen = false;
    std::string defaultScenePath;


    void init(int argc, char **argv)
    {
        YA_CORE_INFO(FUNCTION_SIG);
        params
            .opt<int>("w", {"width"}, "Window width")
            .opt<int>("h", {"height"}, "Window height")
            .opt<bool>("f", {"fullscreen"}, "Fullscreen mode", "false")
            .parse(argc, argv);

        title = params._opt.program();
        params.tryGet<int>("width", width);
        params.tryGet<int>("height", height);
        params.tryGet<bool>("fullscreen", fullscreen);
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
    static App *_instance;

    // Core subsystems
    IRender      *_render       = nullptr;
    SceneManager *_sceneManager = nullptr;
    ImGuiManager *_imguiManager = nullptr;

    std::shared_ptr<IRenderPass>                 _renderpass;
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;

    std::shared_ptr<ShaderStorage> _shaderStorage = nullptr;

    // Runtime state
    bool          bRunning         = true;
    ERenderAPI::T currentRenderAPI = ERenderAPI::None;

    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;

    uint32_t _frameIndex = 0;
    bool     _bPause     = false;
    bool     _bMinimized = false; // Track window minimized state

    AppDesc   _ci;
    glm::vec2 _windowSize = {0, 0};

    // Input and Camera
    InputManager inputManager;
    TaskManager  taskManager;
    FreeCamera   camera;

    // Application state
    AppMode   _appMode      = AppMode::Control;
    glm::vec2 _lastMousePos = {0, 0};

    std::shared_ptr<IRenderTarget> _rt = nullptr;

  public:
    App()          = default;
    virtual ~App() = default;

    void init(AppDesc ci);
    int  run();
    int  iterate(float dt);
    void quit();
    int  processEvent(SDL_Event &event);
    template <typename T>
    int dispatchEvent(const T &event)
    {
        if (0 == onEvent(event)) {
            MessageBus::get()->publish(event);
        }
        return 0;
    }

    void requestQuit() { bRunning = false; }

    // before render context created
    virtual void onInit(AppDesc ci);
    //  after render context created
    virtual void onPostInit();
    virtual void onQuit() {}


    virtual int  onEvent(const Event &event);
    virtual void onUpdate(float dt);
    virtual void onRender(float dt);
    virtual void onRenderGUI();



    static App *get() { return _instance; }

    // Getters for subsystems
    [[nodiscard]] SceneManager *getSceneManager() const { return _sceneManager; }
    [[nodiscard]] ImGuiManager *getImGuiManager() const { return _imguiManager; }

    [[nodiscard]] IRender *getRender() const { return _render; }
    template <typename T>
    [[nodiscard]] T *getRender() { return static_cast<T *>(getRender()); }

    [[nodiscard]] const AppDesc                 &getCI() const { return _ci; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }

    [[nodiscard]] Scene *getScene() const;

    [[nodiscard]] uint32_t getFrameIndex() const { return _frameIndex; }
    // time count from app started
    [[nodiscard]] uint64_t getElapsedTimeMS() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - _startTime).count();
    }

    // temp
    [[nodiscard]] IRenderTarget      *getRenderTarget() const { return _rt.get(); }
    [[nodiscard]] const InputManager &getInputManager() const { return inputManager; }

  protected:
    // Protected for derived classes to override
    virtual void onSceneInit(Scene *scene);
    virtual void onSceneDestroy(Scene *scene) {}

  private:

    bool loadScene(const std::string &path);
    bool unloadScene();
    bool saveScene(const std::string &path) { return false; } // TODO: implement

    bool                           onWindowResized(const WindowResizeEvent &event);
    bool                           onKeyReleased(const KeyReleasedEvent &event);
    bool                           onMouseMoved(const MouseMoveEvent &event);
    bool                           onMouseButtonReleased(const MouseButtonReleasedEvent &event);
    bool                           onMouseScrolled(const MouseScrolledEvent &event);
    [[nodiscard]] const glm::vec2 &getWindowSize() const { return _windowSize; }



    void imcDrawMaterials();


    void handleSystemSignals();
};


} // namespace ya