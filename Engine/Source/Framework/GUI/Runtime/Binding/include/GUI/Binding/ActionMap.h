#pragma once

#include "Core/Api.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

enum class EActionResult : uint8_t
{
    Missing,
    Disabled,
    Ran,
};

/// Keyboard chord for an action. `bPrimary` is Ctrl on Windows/Linux and
/// Meta/Cmd on macOS so File-menu accelerators share one registration.
struct FActionChord
{
    EKey::T key      = EKey::NONE;
    bool    bPrimary = false;
    bool    bShift   = false;
    bool    bAlt     = false;
    bool    bCtrl    = false;

    [[nodiscard]] static FActionChord primary(EKey::T key, bool bShift = false);
    [[nodiscard]] bool empty() const { return key == EKey::NONE; }
    [[nodiscard]] bool hasModifier() const { return bPrimary || bShift || bAlt || bCtrl; }
    [[nodiscard]] bool matches(const KeyPressedEvent& event) const;
    [[nodiscard]] std::string label() const;
    [[nodiscard]] bool operator==(const FActionChord&) const = default;
};

struct FAction
{
    std::string           id;
    std::string           label;
    FActionChord          chord;
    std::function<void()> execute;
    std::function<bool()> canExecute;
};

/// Identity command table. Menus, shortcuts, toolbars, context menus and a
/// command palette all `execute(id)` — they do not keep parallel lambdas.
class YA_GUI_API ActionMap
{
  public:
    [[nodiscard]] bool define(FAction action);
    [[nodiscard]] const FAction* find(const std::string& id) const;
    [[nodiscard]] EActionResult execute(const std::string& id);
    [[nodiscard]] bool dispatchKey(const KeyPressedEvent& event, bool bTextInput);
    [[nodiscard]] const std::vector<std::string>& ids() const { return _ids; }
    [[nodiscard]] bool enabled(const std::string& id) const;

  private:
    std::vector<std::string>                  _ids;
    std::unordered_map<std::string, FAction>  _byId;
};

} // namespace ya
