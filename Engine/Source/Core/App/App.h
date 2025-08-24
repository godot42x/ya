#pragma once

#include "Render/Shader.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Base.h"
#include "Core/Input/InputManager.h"
#include "Platform/Render/Vulkan/VulkanQueue.h"


#include "ImGuiHelper.h"
#include "Render/Render.h"
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


    void init(int argc, char **argv)
    {
        YA_CORE_INFO(FUNCTION_SIG);
        params
            .opt<int>("w", {"width"}, "Window width")
            .opt<int>("h", {"height"}, "Window height")
            .opt<bool>("f", {"fullscreen"}, "Fullscreen mode", "false")
            .parse(argc, argv);

        params.tryGet<std::string>("title", title);
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
    IWindowProvider  *windowProvider       = nullptr;
    IRender          *_render              = nullptr;
    VulkanRenderPass *m_triangleRenderPass = nullptr;
    VulkanRenderPass *m_2DRenderPass       = nullptr;

    std::shared_ptr<ShaderStorage> _shaderStorage = nullptr;


    InputManager inputManager;


    bool          bRunning            = true;
    ERenderAPI::T currentRenderAPI    = ERenderAPI::None;
    VulkanQueue  *_firstGraphicsQueue = nullptr;
    VulkanQueue  *_firstPresentQueue  = nullptr;

    int swapchainImageSize = -1;


    using clock_t      = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;
    time_point_t _lastTime;
    time_point_t _startTime;
    uint32_t     _frameIndex = 0;
    bool         _bPause     = false;

    AppCreateInfo _ci;

    TaskManager taskManager;

  public:
    App()          = default;
    virtual ~App() = default;

    void init(AppCreateInfo ci);
    int  run();
    int  iterate(float dt);
    void quit();

    virtual void onInit(AppCreateInfo ci) {}
    virtual void onQuit() {}


    virtual void onUpdate(float dt);
    virtual void onRender(float dt);
    virtual int  onEvent(SDL_Event &event);

    void onDraw(float dt);

    static App *get() { return _instance; }

    template <typename T>
    [[nodiscard]] T       *getRender() { return static_cast<T *>(_render); }
    [[nodiscard]] IRender *getRender() { return _render; }

    [[nodiscard]] const AppCreateInfo           &getCI() const { return _ci; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }


  private:



    void initSemaphoreAndFence();
    void releaseSemaphoreAndFence();
};


} // namespace ya