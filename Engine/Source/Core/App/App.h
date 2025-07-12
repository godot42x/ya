#pragma once

#include "Core/Input/InputManager.h"
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


namespace Neon
{

class App
{
    static App *_instance;

    // StackDeleter    deleteStack;
    WindowProvider *windowProvider = nullptr;
    IRender        *vkRender       = nullptr;

    InputManager inputManager;


    bool bRunning = true;

  public:
    static App *create();
    void        init();
    int         run();
    void        quit();

    int onEvent(SDL_Event &event);

    int iterate(float dt);

    IRender    *getRender() { return vkRender; }
    static App *get() { return _instance; }
};

} // namespace Neon