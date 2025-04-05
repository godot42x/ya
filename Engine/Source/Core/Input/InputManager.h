#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>


#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>

#include "Core/Event.h"


enum class KeyState
{
    Released,
    Pressed
};

class InputManager
{

  private:
    std::unordered_map<SDL_Keycode, KeyState> currentKeyStates;
    std::unordered_map<SDL_Keycode, KeyState> previousKeyStates;

    std::unordered_map<Uint8, KeyState> currentMouseStates;
    std::unordered_map<Uint8, KeyState> previousMouseStates;

    glm::vec2 mousePosition{0.0f, 0.0f};
    glm::vec2 previousMousePosition{0.0f, 0.0f};
    glm::vec2 mouseDelta{0.0f, 0.0f};

  public:
    InputManager();
    ~InputManager() = default;

    void                   update();
    EventProcessState processEvent(const SDL_Event &event);

    bool isKeyPressed(SDL_Keycode keycode) const;
    bool wasKeyPressed(SDL_Keycode keycode) const;
    bool wasKeyReleased(SDL_Keycode keycode) const;

    bool isMouseButtonPressed(Uint8 button) const;
    bool wasMouseButtonPressed(Uint8 button) const;
    bool wasMouseButtonReleased(Uint8 button) const;

    glm::vec2 getMousePosition() const { return mousePosition; }
    glm::vec2 getMouseDelta() const { return mouseDelta; }
};
