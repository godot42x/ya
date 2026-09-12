#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// Search combo (minimal): a combo whose popup list is filtered by typed
/// text. Focus + KeyTyped updates the filter and re-opens the filtered menu;
/// clicking an entry selects it. The host supplies the items.
struct YA_GUI_API UISearchComboBox : public UIElement, public UIStyledWidget<UISearchComboBox, FSearchComboStyle>
{
    YA_REFLECT_BEGIN(UISearchComboBox, UIElement)
    YA_REFLECT_FIELD(_selectedIndex, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FSearchComboStyle)

    explicit UISearchComboBox(std::string name = "SearchComboBox") : UIElement(std::move(name), "searchcombo")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISearchComboBox>; }

    std::vector<std::string> _items;
    int      _selectedIndex = -1;
    std::string _filter;
    /// Default ignore-case; set true for a literal substring match.
    bool        _bCaseSensitive = false;
    uint32_t    _fontSize = 13;

    std::function<void(int index)> _onSelectionChanged;

    [[nodiscard]] const std::string& currentLabel() const
    {
        static const std::string kEmpty;
        return _selectedIndex >= 0 && _selectedIndex < static_cast<int>(_items.size())
                   ? _items[_selectedIndex]
                   : kEmpty;
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "searchComboBox"},
                               {"selectedIndex", _selectedIndex},
                               {"filter", _filter}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }
    void onFocusGained(bool /*bFromKeyboard*/) override { _bFocused = true; }
    void onFocusLost() override { _bFocused = false; }
    [[nodiscard]] bool wantsTextInput() const override { return _bFocused; }
    void clearTransientInputState() override;

  private:
    /// Open (or refresh) the filtered popup menu below the control.
    void openFilteredMenu();
    void closeMenu();
    [[nodiscard]] std::vector<int> filteredIndices() const;

    std::shared_ptr<struct UIMenu> _openMenu;
    /// True while openFilteredMenu refreshes (closing the old menu): the
    /// old menu's dismiss must not clear the filter, only a real close
    /// (pick / outside click / Esc) does.
    bool _bRefreshingMenu = false;
    VisualFlag _bHovered{*this};
    VisualFlag _bFocused{*this};
};

} // namespace ya
