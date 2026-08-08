#include "InputManager.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>

#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cctype>
#include <optional>

#include "Core/Event.h"
#include "Core/MessageBus.h"

namespace ya
{
namespace
{

std::string normalizeBindingToken(std::string token)
{
    std::string normalized;
    normalized.reserve(token.size());
    for (const unsigned char ch : token) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}

std::optional<EKey::T> parseKeyBindingToken(const std::string& token)
{
    const std::string normalized = normalizeBindingToken(token);
    if (normalized.empty()) {
        return std::nullopt;
    }

    if (normalized.size() == 1) {
        const char ch = normalized.front();
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<EKey::T>(static_cast<int>(EKey::K_A) + (ch - 'A'));
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<EKey::T>(static_cast<int>(EKey::K_0) + (ch - '0'));
        }
    }

    static const std::unordered_map<std::string, EKey::T> keyMap = {
        {"SPACE", EKey::Space},
        {"ENTER", EKey::Enter},
        {"RETURN", EKey::Enter},
        {"ESC", EKey::Escape},
        {"ESCAPE", EKey::Escape},
        {"TAB", EKey::Tab},
        {"BACKSPACE", EKey::Backspace},
        {"LEFT", EKey::Left},
        {"RIGHT", EKey::Right},
        {"UP", EKey::Up},
        {"DOWN", EKey::Down},
        {"DELETE", EKey::Delete},
        {"DEL", EKey::Delete},
        {"INSERT", EKey::Insert},
        {"HOME", EKey::Home},
        {"END", EKey::End},
        {"PAGEUP", EKey::Pageup},
        {"PAGEDOWN", EKey::PagedowN},
        {"LSHIFT", EKey::LShift},
        {"RSHIFT", EKey::RShift},
        {"LCTRL", EKey::LCtrl},
        {"RCTRL", EKey::RCtrl},
        {"LALT", EKey::LAlt},
        {"RALT", EKey::RAlt},
        {"LMETA", EKey::LMeta},
        {"RMETA", EKey::RMeta},
    };

    if (normalized.size() >= 2 && normalized.front() == 'F') {
        const std::string suffix = normalized.substr(1);
        if (suffix == "1") return EKey::F1;
        if (suffix == "2") return EKey::F2;
        if (suffix == "3") return EKey::F3;
        if (suffix == "4") return EKey::F4;
        if (suffix == "5") return EKey::F5;
        if (suffix == "6") return EKey::F6;
        if (suffix == "7") return EKey::F7;
        if (suffix == "8") return EKey::F8;
        if (suffix == "9") return EKey::F9;
        if (suffix == "10") return EKey::F10;
        if (suffix == "11") return EKey::F11;
        if (suffix == "12") return EKey::F12;
    }

    if (auto it = keyMap.find(normalized); it != keyMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<EMouse::T> parseMouseBindingToken(const std::string& token)
{
    const std::string normalized = normalizeBindingToken(token);
    static const std::unordered_map<std::string, EMouse::T> mouseMap = {
        {"MOUSELEFT", EMouse::Left},
        {"LEFTMOUSE", EMouse::Left},
        {"LEFTCLICK", EMouse::Left},
        {"MOUSEMIDDLE", EMouse::Middle},
        {"MIDDLEMOUSE", EMouse::Middle},
        {"MOUSERIGHT", EMouse::Right},
        {"RIGHTMOUSE", EMouse::Right},
        {"RIGHTCLICK", EMouse::Right},
        {"MOUSEX1", EMouse::X1},
        {"MOUSEX2", EMouse::X2},
    };

    if (auto it = mouseMap.find(normalized); it != mouseMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

void bindDefaultGameplayActions(InputManager& input)
{
    input.clearActionBindings();
    input.bindAction("move_forward", EKey::K_W);
    input.bindAction("move_back", EKey::K_S);
    input.bindAction("move_left", EKey::K_A);
    input.bindAction("move_right", EKey::K_D);
    input.bindAction("move_up", EKey::K_Q);
    input.bindAction("move_down", EKey::K_E);
    input.bindAction("look", EMouse::Right);
}

} // namespace


InputManager::InputManager()
{
}

InputManager::~InputManager()
{
}

void InputManager::init()
{
    bindDefaultGameplayActions(*this);
}

void InputManager::preUpdate()
{
    previousKeyStates   = currentKeyStates;
    previousMouseStates = currentMouseStates;
    previousMousePosition = mousePosition;
    mouseDelta           = {0.0f, 0.0f};
    _mouseScrollDelta    = {0.0f, 0.0f};
}

void InputManager::postUpdate()
{
}

void InputManager::cancelInput()
{
    currentKeyStates.clear();
    previousKeyStates.clear();
    currentMouseStates.clear();
    previousMouseStates.clear();
    mouseDelta           = {0.0f, 0.0f};
    _mouseScrollDelta    = {0.0f, 0.0f};
    previousMousePosition = mousePosition;
}

void InputManager::clearActionBindings()
{
    _actionBindings.clear();
}

void InputManager::bindAction(const std::string& actionName, EKey::T key)
{
    _actionBindings[actionName].keys.push_back(key);
}

void InputManager::bindAction(const std::string& actionName, EMouse::T button)
{
    _actionBindings[actionName].mouseButtons.push_back(button);
}

void InputManager::configureActionBindings(const std::unordered_map<std::string, std::vector<std::string>>& actionBindings)
{
    if (actionBindings.empty()) {
        bindDefaultGameplayActions(*this);
        return;
    }

    clearActionBindings();
    for (const auto& [actionName, bindings] : actionBindings) {
        if (actionName.empty()) {
            continue;
        }

        for (const auto& binding : bindings) {
            if (auto key = parseKeyBindingToken(binding)) {
                bindAction(actionName, *key);
                continue;
            }
            if (auto button = parseMouseBindingToken(binding)) {
                bindAction(actionName, *button);
                continue;
            }

            YA_CORE_WARN("Unknown input binding '{}' for action '{}'", binding, actionName);
        }
    }
}

bool InputManager::isActionPressed(std::string_view actionName) const
{
    const auto it = _actionBindings.find(actionName);
    if (it == _actionBindings.end()) {
        return false;
    }

    for (const auto key : it->second.keys) {
        if (isKeyPressed(key)) {
            return true;
        }
    }
    for (const auto button : it->second.mouseButtons) {
        if (isMouseButtonPressed(button)) {
            return true;
        }
    }
    return false;
}

bool InputManager::wasActionPressed(std::string_view actionName) const
{
    const auto it = _actionBindings.find(actionName);
    if (it == _actionBindings.end()) {
        return false;
    }

    for (const auto key : it->second.keys) {
        if (wasKeyPressed(key)) {
            return true;
        }
    }
    for (const auto button : it->second.mouseButtons) {
        if (wasMouseButtonPressed(button)) {
            return true;
        }
    }
    return false;
}

bool InputManager::wasActionReleased(std::string_view actionName) const
{
    const auto it = _actionBindings.find(actionName);
    if (it == _actionBindings.end()) {
        return false;
    }

    for (const auto key : it->second.keys) {
        if (wasKeyReleased(key)) {
            return true;
        }
    }
    for (const auto button : it->second.mouseButtons) {
        if (wasMouseButtonReleased(button)) {
            return true;
        }
    }
    return false;
}

EventProcessState InputManager::processEvent(const Event &event)
{
    switch (event.getEventType()) {
    case EEvent::KeyPressed:
        setKeyState(static_cast<const KeyPressedEvent &>(event).getKeyCode(), KeyState::Pressed);
        break;
    case EEvent::KeyReleased:
        setKeyState(static_cast<const KeyReleasedEvent &>(event).getKeyCode(), KeyState::Released);
        break;
    case EEvent::MouseButtonPressed:
        setMouseState(static_cast<const MouseButtonPressedEvent &>(event).GetMouseButton(), KeyState::Pressed);
        break;
    case EEvent::MouseButtonReleased:
        setMouseState(static_cast<const MouseButtonReleasedEvent &>(event).GetMouseButton(), KeyState::Released);
        break;
    case EEvent::MouseScrolled:
    {
        const auto &e     = static_cast<const MouseScrolledEvent &>(event);
        _mouseScrollDelta = {e._offsetX, e._offsetY};
    } break;
    case EEvent::MouseMoved:
    {
        const auto& e = static_cast<const MouseMoveEvent&>(event);
        setMousePosition({e.getX(), e.getY()}, {e.getDeltaX(), e.getDeltaY()});
    } break;
    default:
        break;
    }
    return EventProcessState::Continue;
}

bool InputManager::isKeyPressed(EKey::T keycode) const
{
    auto it = currentKeyStates.find(keycode);
    return it != currentKeyStates.end() && it->second == KeyState::Pressed;
}

bool InputManager::wasKeyPressed(EKey::T keycode) const
{
    auto curr = currentKeyStates.find(keycode);
    auto prev = previousKeyStates.find(keycode);

    return (curr != currentKeyStates.end() && curr->second == KeyState::Pressed) &&
           (prev == previousKeyStates.end() || prev->second == KeyState::Released);
}

bool InputManager::wasKeyReleased(EKey::T keycode) const
{
    auto curr = currentKeyStates.find(keycode);
    auto prev = previousKeyStates.find(keycode);

    return (curr != currentKeyStates.end() && curr->second == KeyState::Released) &&
           (prev != previousKeyStates.end() && prev->second == KeyState::Pressed);
}

bool InputManager::isMouseButtonPressed(EMouse::T button) const
{
    auto it = currentMouseStates.find(button);
    return it != currentMouseStates.end() && it->second == KeyState::Pressed;
}

bool InputManager::wasMouseButtonPressed(EMouse::T button) const
{
    auto curr = currentMouseStates.find(button);
    auto prev = previousMouseStates.find(button);

    return (curr != currentMouseStates.end() && curr->second == KeyState::Pressed) &&
           (prev == previousMouseStates.end() || prev->second == KeyState::Released);
}

bool InputManager::wasMouseButtonReleased(EMouse::T button) const
{
    auto curr = currentMouseStates.find(button);
    auto prev = previousMouseStates.find(button);

    return (curr != currentMouseStates.end() && curr->second == KeyState::Released) &&
           (prev != previousMouseStates.end() && prev->second == KeyState::Pressed);
}

glm::vec2 InputManager::getMouseScrollDelta() const
{
    return _mouseScrollDelta;
}

void InputManager::setMousePosition(glm::vec2 position, glm::vec2 delta)
{
    mouseDelta += delta;
    mousePosition = position;
}

} // namespace ya
