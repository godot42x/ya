#pragma once
#include "Core/Base.h"

#include "Core/Camera.h"
#include "Render/Shader.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Input/InputManager.h"
#include "Platform/Render/Vulkan/VulkanQueue.h"


#include "Render/Render.h"
#include "Scene/Scene.h"
#include "WindowProvider.h"

#include "ClLIParams.h"


// #include "utility.cc/stack_deleter.h"

struct LogicalDevice;
struct VulkanRenderPass;
struct VulkanPipelineLayout;

namespace ya
{



struct AppCreateInfo
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

    // StackDeleter    deleteStack;
    IRender *_render = nullptr;
    // VulkanRenderPass *m_triangleRenderPass = nullptr;
    // VulkanRenderPass *m_2DRenderPass       = nullptr;
    std::shared_ptr<ShaderStorage> _shaderStorage = nullptr;


    bool          bRunning            = true;
    ERenderAPI::T currentRenderAPI    = ERenderAPI::None;
    VulkanQueue  *_firstGraphicsQueue = nullptr;
    VulkanQueue  *_firstPresentQueue  = nullptr;

    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;
    uint32_t     _frameIndex = 0;
    bool         _bPause     = false;

    AppCreateInfo _ci;
    glm::vec2     _windowSize = {0, 0};

    InputManager inputManager;
    TaskManager  taskManager;
    FreeCamera   camera;


    std::unique_ptr<Scene> _scene = nullptr;


  public:
    App()          = default;
    virtual ~App() = default;

    void init(AppCreateInfo ci);
    int  run();
    int  iterate(float dt);
    void quit();
    int  processEvent(SDL_Event &event);
    void requestQuit() { bRunning = false; }

    virtual void onInit(AppCreateInfo ci);
    virtual void onQuit() {}


    virtual void onUpdate(float dt);
    virtual void onRender(float dt);
    virtual int  onEvent(const Event &event);
    virtual void onRenderGUI();



    static App *get() { return _instance; }

    template <typename T>
    [[nodiscard]] T       *getRender() { return static_cast<T *>(_render); }
    [[nodiscard]] IRender *getRender() { return _render; }

    [[nodiscard]] const AppCreateInfo           &getCI() const { return _ci; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }

    [[nodiscard]] Scene *getScene() const { return _scene.get(); }

  private:

    bool         loadScene(const std::string &path);
    bool         unloadScene();
    bool         saveScene(const std::string &path) {}
    virtual void onSceneInit(Scene *scene);
    virtual void onSceneDestroy(Scene *scene) {}

    bool                           onWindowResized(const WindowResizeEvent &event);
    bool                           onKeyReleased(const KeyReleasedEvent &event);
    [[nodiscard]] const glm::vec2 &getWindowSize() const { return _windowSize; }
};


} // namespace ya