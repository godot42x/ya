#include "ActionMap.h"

namespace ya
{

namespace
{

bool primaryPressed(const KeyEvent& event)
{
#if defined(__APPLE__)
    return event.isMetaPressed();
#else
    return event.isCtrlPressed();
#endif
}

char letterForKey(EKey::T key)
{
    if (key >= EKey::K_A && key <= EKey::K_Z) {
        return static_cast<char>('A' + (key - EKey::K_A));
    }
    return 0;
}

} // namespace

FActionChord FActionChord::primary(EKey::T key, bool bShift)
{
    FActionChord chord;
    chord.key      = key;
    chord.bPrimary = true;
    chord.bShift   = bShift;
    return chord;
}

bool FActionChord::matches(const KeyPressedEvent& event) const
{
    if (empty() || event.bRepeat || event.getKeyCode() != key) {
        return false;
    }
    if (bPrimary != primaryPressed(event)) {
        return false;
    }
    if (bShift != event.isShiftPressed() || bAlt != event.isAltPressed()) {
        return false;
    }
#if defined(__APPLE__)
    if (bCtrl != event.isCtrlPressed()) {
        return false;
    }
#endif
    return true;
}

std::string FActionChord::label() const
{
    if (empty()) {
        return {};
    }
    std::string text;
    if (bPrimary) {
#if defined(__APPLE__)
        text += "Cmd+";
#else
        text += "Ctrl+";
#endif
    }
    if (bCtrl) {
        text += "Ctrl+";
    }
    if (bAlt) {
        text += "Alt+";
    }
    if (bShift) {
        text += "Shift+";
    }
    if (const char letter = letterForKey(key)) {
        text += letter;
    }
    else {
        text += EKey::toString(key);
    }
    return text;
}

bool ActionMap::define(FAction action)
{
    if (action.id.empty() || !action.execute || _byId.contains(action.id)) {
        return false;
    }
    if (!action.chord.empty()) {
        for (const auto& [id, existing] : _byId) {
            (void)id;
            if (existing.chord == action.chord) {
                return false;
            }
        }
    }
    _ids.push_back(action.id);
    _byId.emplace(action.id, std::move(action));
    return true;
}

const FAction* ActionMap::find(const std::string& id) const
{
    const auto it = _byId.find(id);
    return it == _byId.end() ? nullptr : &it->second;
}

bool ActionMap::enabled(const std::string& id) const
{
    const FAction* action = find(id);
    return action && (!action->canExecute || action->canExecute());
}

EActionResult ActionMap::execute(const std::string& id)
{
    const FAction* action = find(id);
    if (!action) {
        return EActionResult::Missing;
    }
    if (action->canExecute && !action->canExecute()) {
        return EActionResult::Disabled;
    }
    action->execute();
    return EActionResult::Ran;
}

bool ActionMap::dispatchKey(const KeyPressedEvent& event, bool bTextInput)
{
    for (const std::string& id : _ids) {
        const FAction& action = _byId.at(id);
        if (!action.chord.matches(event)) {
            continue;
        }
        if (bTextInput && !action.chord.hasModifier()) {
            continue;
        }
        return execute(id) == EActionResult::Ran;
    }
    return false;
}

} // namespace ya
