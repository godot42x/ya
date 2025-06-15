#pragma once

#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

#include "../ImGuiHelper.h"
#include "StackDeleter.h"
#include "WindowProvider.h"


#include "Platform/Render/Vulkan/VulkanDevice.h"

// #include "utility.cc/stack_deleter.h"


namespace Neon
{

class App
{
    // StackDeleter    deleteStack;
    WindowProvider *windowProvider = nullptr;
    LogicalDevice  *logicalDevice  = nullptr;


    bool bRunning = true;

  public:
    void init();
    int  run();
    void quit();

    int onEvent(SDL_Event &event);

    int iterate(float dt)
    {
        // Handle input, update logic, render, etc.
        // This is where the main loop logic would go.
        return 0; // Continue running
    }
};

} // namespace Neon