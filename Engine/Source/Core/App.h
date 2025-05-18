#pragma once

#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

#include "../ImGuiHelper.h"
#include "../Render/Model.h"
#include "../Render/SDL/SDLGPUCommandBuffer.h"
#include "../Render/SDL/SDLGPURender.h"
#include "AssetManager.h"
#include "EditorCamera.h"
#include "Input/InputManager.h"
#include "UI/DialogWindow.h"


namespace NeonEngine
{

class App
{
};

} // namespace NeonEngine