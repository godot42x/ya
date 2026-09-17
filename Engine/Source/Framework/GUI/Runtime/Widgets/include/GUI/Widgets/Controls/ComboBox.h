#pragma once

#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// Drop-down combo box (gui-app-bootstrap Phase 4 tool primitive).
///
/// Collapsed state shows the current selection in a button-like field; a
/// click (or Space/Enter/Down on the focused box) opens a UIMenu; the menu
/// prefers below the field and flips above when it would clip out of the
/// window. Selecting an item fires `_onSelectionChanged` and closes the menu.
struct YA_GUI_API UIComboBox : public UIElement, public UIStyledWidget<UIComboBox, FComboBoxStyle>
{
    YA_REFLECT_BEGIN(UIComboBox, UIElement)
    YA_REFLECT_FIELD(_items, .instanceEditable())
    YA_REFLECT_FIELD(_selectedIndex, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FComboBoxStyle)

    explicit UIComboBox(std::string name = "ComboBox") : UIElement(std::move(name), "combobox")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIComboBox>; }

    std::vector<std::string> _items;
    int                      _selectedIndex = -1;
    uint32_t                 _fontSize      = 13;

    /// Fired after the selection changes (index in [0, items.size())).
    std::function<void(int selectedIndex)> _onSelectionChanged;

    /// Programmatic selection (also fires the callback).
    void select(int index);
    /// Presenter sync: optional notify so model writes do not re-enter as edits.
    void setSelectedIndex(int index, bool bNotify = true);
    void setMixed(bool mixed);
    [[nodiscard]] bool isMixed() const { return _bMixed; }
    /// Current label or "" when nothing selected.
    [[nodiscard]] std::string currentLabel() const
    {
        if (_bMixed) {
            return "—";
        }
        return (_selectedIndex >= 0 && _selectedIndex < static_cast<int>(_items.size()))
                   ? _items[static_cast<size_t>(_selectedIndex)]
                   : "";
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; }

  private:
    /// Open the dropdown, flipping above the field if it would clip.
    void openDropdown();
    VisualFlag _bHovered{*this};
    bool       _bMixed = false;
};

} // namespace ya
