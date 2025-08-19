#pragma once

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
    int         width      = 800;
    int         height     = 600;
    bool        fullscreen = false;


    void init(int argc, char **argv)
    {
        NE_CORE_INFO(FUNCTION_SIG);
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


struct App
{
    static App *_instance;

    // StackDeleter    deleteStack;
    IWindowProvider  *windowProvider       = nullptr;
    IRender          *_render              = nullptr;
    VulkanRenderPass *m_triangleRenderPass = nullptr;
    VulkanRenderPass *m_2DRenderPass       = nullptr;


    VulkanPipelineLayout *defaultPipelineLayout = nullptr;

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


  public:
    App()          = delete;
    virtual ~App() = default;

    App(AppCreateInfo ci)
    {
        _ci = ci;
        NE_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
        _instance = this;
    }
    void init();
    int  run();
    void quit();


    int          iterate(float dt);
    virtual void onUpdate(float dt);
    virtual void onRender(float dt);
    virtual int  onEvent(SDL_Event &event);

    void onDraw(float dt);

    IRender    *getRender() { return _render; }
    static App *get() { return _instance; }

    [[nodiscard]] const AppCreateInfo &getCI() const { return _ci; }


  private:



    void initSemaphoreAndFence();
    void releaseSemaphoreAndFence();
};


} // namespace ya