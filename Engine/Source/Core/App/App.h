#pragma once

#include "Core/Input/InputManager.h"
#include "Platform/Render/Vulkan/VulkanQueue.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

#include "ImGuiHelper.h"
#include "Render/Render.h"
#include "WindowProvider.h"



// #include "utility.cc/stack_deleter.h"

struct LogicalDevice;
struct VulkanRenderPass;
struct VulkanPipelineLayout;

namespace ya
{



struct App
{
    static App *_instance;

    // StackDeleter    deleteStack;
    WindowProvider   *windowProvider       = nullptr;
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



  public:
    App()
    {
        NE_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
        _instance = this;
    }
    void init();
    int  run();
    void quit();


    int  iterate(float dt);
    void onUpdate(float dt);
    void onDraw(float dt);
    int  onEvent(SDL_Event &event);

    IRender    *getRender() { return _render; }
    static App *get() { return _instance; }


  private:



    void initSemaphoreAndFence();
    void releaseSemaphoreAndFence();
};


} // namespace ya