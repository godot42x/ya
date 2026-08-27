#pragma once

#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

struct UIMenu;

/// One popup menu entry. Hover highlighting is driven by the owning UIMenu
/// (single highlight, keyboard sync); the item only reports hover enters.
struct YA_GUI_API UIMenuItem : public UIElement, public UIStyledWidget<UIMenuItem, FMenuStyle>
{
    YA_REFLECT_BEGIN(UIMenuItem, UIElement)
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FMenuStyle)

    explicit UIMenuItem(std::string name = "MenuItem") : UIElement(std::move(name), "menu")
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIMenuItem>; }

    std::string _label;
    uint32_t    _fontSize    = 13;
    VisualFlag  _bHighlighted{*this};

    /// Fired on click. The owning UIMenu closes itself after the action.
    std::function<void()> _onAction;
    /// Fired when the pointer enters this item (menu syncs the highlight).
    std::function<void(UIMenuItem*)> _onHovered;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
};

/// Popup menu (based on UIPopupOverlay): a panel of UIMenuItem rows with
/// mouse hover + keyboard navigation (Up/Down/Enter/Esc).
///
/// Build via create(); open via openAt(tree, pos) which anchors the top-left
/// of the menu at `pos`. Selecting an item runs its action and closes.
struct YA_GUI_API UIMenu : public UIPopupOverlay, public UIStyledWidget<UIMenu, FMenuStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FMenuStyle)

    struct FItem
    {
        std::string              label;
        std::function<void()>    action;
    };

    explicit UIMenu(std::string name = "Menu") : UIPopupOverlay(std::move(name), "menu") {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIMenu>; }

    /// Menu panel metrics.
    float       _itemHeight = 26.0f;
    float       _panelPadding = 4.0f;
    uint32_t    _fontSize = 13;

    /// Create a menu with `items`; the returned menu is detached until open().
    static std::shared_ptr<UIMenu> create(const std::vector<FItem>& items);

    /// Attach to `tree`'s Popup layer with the menu top-left at `pos`.
    void openAt(WidgetTree& tree, const glm::vec2& pos);

    /// Highlight + return the item at `index` (clamped, cyclic for keyboard).
    void setHighlight(int index);
    [[nodiscard]] int getHighlightIndex() const { return _highlightIndex; }
    [[nodiscard]] std::vector<UIMenuItem*> menuItems() const;

    void layoutAssigned(const Rect2D& rect) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  private:
    void rebuildContent(const std::vector<FItem>& items);
    void activateHighlighted();

    int _highlightIndex = -1;
    /// Menu content size (panel rect extent) derived from the item rows in
    /// rebuildContent: row width = maxLabelWidth + 20, height = N x
    /// _itemHeight, plus the panel padding on both axes.
    glm::vec2 _contentExtent = {0.0f, 0.0f};
    std::vector<UIMenuItem*> _items;
};

} // namespace ya
