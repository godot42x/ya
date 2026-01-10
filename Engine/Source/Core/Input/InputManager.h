#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>


#include <glm/glm.hpp>
#include <unordered_map>

#include "Core/Event.h"
#include "Core/KeyCode.h"

namespace ya
{


enum class KeyState
{
    Released,
    Pressed
};

struct InputManager
{
    friend struct App;

  private:
    std::unordered_map<EKey::T, KeyState> currentKeyStates;
    std::unordered_map<EKey::T, KeyState> previousKeyStates;

    std::unordered_map<EMouse::T, KeyState> currentMouseStates;
    std::unordered_map<EMouse::T, KeyState> previousMouseStates;

    glm::vec2 mousePosition{0.0f, 0.0f};
    glm::vec2 previousMousePosition{0.0f, 0.0f};
    glm::vec2 mouseDelta{0.0f, 0.0f};
    glm::vec2 _mouseScrollDelta{0, 0};

  public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager &)            = delete;
    InputManager &operator=(const InputManager &) = delete;
    InputManager(InputManager &&) noexcept        = default;
    InputManager &operator=(InputManager &&)      = default;

    void init();
    void preUpdate();
    void postUpdate();

    bool isKeyPressed(EKey::T keycode) const;
    bool wasKeyPressed(EKey::T keycode) const;
    bool wasKeyReleased(EKey::T keycode) const;


    bool isMouseButtonPressed(EMouse::T button) const;
    bool wasMouseButtonPressed(EMouse::T button) const;
    bool wasMouseButtonReleased(EMouse::T button) const;


    glm::vec2 getMouseScrollDelta() const;

    glm::vec2 getMousePosition() const
    {
        return mousePosition;
    }
    glm::vec2 getMouseDelta() const { return mouseDelta; }

    EventProcessState processEvent(const SDL_Event &event);
    EventProcessState processEvent(const Event &event);

  private:
    void setKeyState(EKey::T keycode, KeyState state) { currentKeyStates[keycode] = state; }
    void setKeyState(SDL_Keycode keycode, KeyState state) { setKeyState(EKey::fromSDLKeycode(keycode), state); }

    void setMouseState(EMouse::T button, KeyState state) { currentMouseStates[button] = state; }
    void setMouseState(Uint8 button, KeyState state) { setMouseState(EMouse::fromSDLMouseButton(button), state); } // from sdl defines
};

} // namespace ya