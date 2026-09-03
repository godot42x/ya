#pragma once

#include "Core/Api.h"
#include "GUI/Binding/Reactive.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

/// Identity-based editor/GUI selection. Widgets bind the Reactive views;
/// the model never stores ECS/Scene pointers.
///
/// `primary` is the inspector target and must be empty or in `selected`.
/// hover / active / focus are independent identities (pointer, keyboard
/// caret, and pane focus) and may name an item that is not selected.
struct YA_GUI_API SelectionModel
{
    SelectionModel();

    void replace(std::vector<std::string> ids, std::string primary = {});
    void select(std::string id);
    void add(std::string id);
    void toggle(std::string id);
    void remove(const std::string& id);
    void clear();
    void clearTransient();

    void setHovered(std::string id);
    void setActive(std::string id);
    void setFocused(std::string id);

    [[nodiscard]] bool contains(const std::string& id) const;
    [[nodiscard]] const std::vector<std::string>& selected() const { return _selected; }
    [[nodiscard]] const std::string& primary() const { return _primary->value(); }
    [[nodiscard]] const std::string& hovered() const { return _hovered->value(); }
    [[nodiscard]] const std::string& active() const { return _active->value(); }
    [[nodiscard]] const std::string& focused() const { return _focused->value(); }
    [[nodiscard]] uint64_t revision() const { return _revision; }

    [[nodiscard]] std::shared_ptr<Reactive<std::string>> primaryRef() const { return _primary; }
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> hoveredRef() const { return _hovered; }
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> activeRef() const { return _active; }
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> focusedRef() const { return _focused; }

  private:
    std::vector<std::string> _selected;
    std::unordered_set<std::string> _index;
    std::shared_ptr<Reactive<std::string>> _primary;
    std::shared_ptr<Reactive<std::string>> _hovered;
    std::shared_ptr<Reactive<std::string>> _active;
    std::shared_ptr<Reactive<std::string>> _focused;
    uint64_t _revision = 0;

    void bump();
    void setPrimary(std::string id);
    void restorePrimaryAfterRemove(const std::string& removed);
};

} // namespace ya
