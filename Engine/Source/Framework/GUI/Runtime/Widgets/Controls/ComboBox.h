#pragma once

#include "GUI/Widgets/Controls/Menu.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// Drop-down combo box (gui-app-bootstrap Phase 4 tool primitive).
///
/// Collapsed state shows the current selection in a button-like field; a
/// click (or Space/Enter/Down on the focused box) opens a UIMenu below the
/// field; selecting an item fires `_onSelectionChanged` and closes the menu.
struct UIComboBox : public UIElement
{
    YA_REFLECT_BEGIN(UIComboBox, UIElement)
    YA_REFLECT_FIELD(_items, .instanceEditable())
    YA_REFLECT_FIELD(_selectedIndex, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_textColor, .instanceEditable())
    YA_REFLECT_FIELD(_fieldColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_END()

    explicit UIComboBox(std::string name = "ComboBox") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIComboBox>; }

    std::vector<std::string> _items;
    int                      _selectedIndex = -1;
    uint32_t                 _fontSize      = 13;

    glm::vec4 _textColor    = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 _fieldColor   = {0.16f, 0.18f, 0.22f, 1.0f};
    glm::vec4 _hoveredColor = {0.22f, 0.25f, 0.30f, 1.0f};

    /// Fired after the selection changes (index in [0, items.size())).
    std::function<void(int selectedIndex)> _onSelectionChanged;

    /// Programmatic selection (also fires the callback).
    void select(int index);
    /// Current label or "" when nothing selected.
    [[nodiscard]] std::string currentLabel() const
    {
        return (_selectedIndex >= 0 && _selectedIndex < static_cast<int>(_items.size()))
                   ? _items[static_cast<size_t>(_selectedIndex)]
                   : "";
    }

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override { _bHovered = false; }

  private:
    /// Open the dropdown menu below the field.
    void openDropdown();
    bool _bHovered = false;
};

} // namespace ya
