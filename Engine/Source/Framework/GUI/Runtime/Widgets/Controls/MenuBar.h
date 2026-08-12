#pragma once

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Menu.h"

#include <functional>
#include <string>

namespace ya
{

/// One menu-bar entry: label button that opens its menu below the bar.
struct UIMenuBarItem : public UIElement
{
    YA_REFLECT_BEGIN(UIMenuBarItem, UIElement)
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_textColor, .instanceEditable())
    YA_REFLECT_FIELD(_normalColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_END()

    explicit UIMenuBarItem(std::string name = "MenuBarItem") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIMenuBarItem>; }

    std::string _label;
    uint32_t    _fontSize    = 13;
    glm::vec4   _textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4   _normalColor = {0.10f, 0.11f, 0.13f, 1.0f};
    glm::vec4   _hoveredColor = {0.20f, 0.22f, 0.27f, 1.0f};

    /// Fired on click / Space / Enter (the bar opens the menu).
    std::function<void()> _onActivate;
    /// Fired when the pointer enters the item (the bar uses this for
    /// hover-open / hover-switch while a menu is already open).
    std::function<void(UIMenuBarItem*)> _onHoveredEnter;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override { _bHovered = false; }

  private:
    bool _bHovered = false;
};

/// Horizontal menu bar. addItem() wires a button that opens `menuFactory`'s
/// menu below the bar; only one menu is open at a time (opening another
/// closes the previous). The bar never owns the menu lifetime (callers keep
/// the tree-alive menu).
struct UIMenuBar : public UIContainer
{
    explicit UIMenuBar(std::string name = "MenuBar") : UIContainer(std::move(name))
    {
        _direction = EWidgetBoxLayout::Horizontal;
        _spacing   = 2.0f;
        _padding   = {4.0f, 4.0f};
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIMenuBar>; }

    /// Add `label`; clicking opens the menu from `menuFactory` (null factory
    /// = disabled entry, only visual). Returns the created item.
    UIMenuBarItem* addItem(const std::string& label, std::function<std::shared_ptr<UIMenu>()> menuFactory);

    /// Close the currently open menu (no-op when none).
    void closeOpenMenu();
    [[nodiscard]] UIMenu* getOpenMenu() const { return _openMenu.get(); }

  private:
    std::shared_ptr<UIMenu> _openMenu;
    /// Item whose menu is currently open (null when none). Drives click
    /// toggle and hover-switch semantics.
    UIMenuBarItem* _openMenuItem = nullptr;
};

} // namespace ya
