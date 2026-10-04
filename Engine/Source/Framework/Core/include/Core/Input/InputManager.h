#pragma once

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Core/Event.h"
#include "Core/KeyCode.h"

namespace ya
{


enum class KeyState
{
    Released,
    Pressed
};

struct YA_CORE_API InputManager
{
    friend struct App;

  private:
    struct ActionBinding
    {
        std::vector<EKey::T>     keys;
        std::vector<EMouse::T>    mouseButtons;
    };

    struct StringHash
    {
        using is_transparent = void;

        size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }

        size_t operator()(const std::string& value) const noexcept
        {
            return operator()(std::string_view(value));
        }
    };

    std::unordered_map<EKey::T, KeyState> currentKeyStates;
    std::unordered_map<EKey::T, KeyState> previousKeyStates;

    std::unordered_map<EMouse::T, KeyState> currentMouseStates;
    std::unordered_map<EMouse::T, KeyState> previousMouseStates;

    std::unordered_map<std::string, ActionBinding, StringHash, std::equal_to<>> _actionBindings;
    /// Unknown names already reported. findAction is const; cleared when the map is replaced.
    mutable std::unordered_set<std::string, StringHash, std::equal_to<>> _warnedMissingActions;
    /// Set once a project map or the built-in defaults have been applied.
    /// init() runs after descriptor apply and must not replace that map.
    bool _bActionMapConfigured = false;

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
    void cancelInput();
    /// Drop held keys without touching mouse buttons or deltas. Used when
    /// keyboard ownership leaves the editor camera so WASD cannot stick.
    void cancelHeldKeys();

    bool isKeyPressed(EKey::T keycode) const;
    bool wasKeyPressed(EKey::T keycode) const;
    bool wasKeyReleased(EKey::T keycode) const;


    bool isMouseButtonDown(EMouse::T button) const { return isMouseButtonPressed(button); }
    bool isMouseButtonPressed(EMouse::T button) const;
    bool wasMouseButtonPressed(EMouse::T button) const;
    bool wasMouseButtonReleased(EMouse::T button) const;

    void clearActionBindings();
    void bindAction(const std::string& actionName, EKey::T key);
    void bindAction(const std::string& actionName, EMouse::T button);
    /// `bDefaultsWhenEmpty`: an omitted map installs the built-in keys. An
    /// InputMap file that is missing or empty does not (`false`).
    void configureActionBindings(const std::unordered_map<std::string, std::vector<std::string>>& actionBindings,
                                 bool bDefaultsWhenEmpty = true);
    [[nodiscard]] bool isActionPressed(std::string_view actionName) const;
    [[nodiscard]] bool wasActionPressed(std::string_view actionName) const;
    [[nodiscard]] bool wasActionReleased(std::string_view actionName) const;

    glm::vec2 getMouseScrollDelta() const;

    glm::vec2 getMousePosition() const
    {
        return mousePosition;
    }
    glm::vec2 getMouseDelta() const { return mouseDelta; }

    EventProcessState processEvent(const Event &event);

  private:
    [[nodiscard]] const ActionBinding* findAction(std::string_view actionName) const;
    void setKeyState(EKey::T keycode, KeyState state) { currentKeyStates[keycode] = state; }
    void setMouseState(EMouse::T button, KeyState state) { currentMouseStates[button] = state; }
    void setMousePosition(glm::vec2 position, glm::vec2 delta);
};

} // namespace ya
