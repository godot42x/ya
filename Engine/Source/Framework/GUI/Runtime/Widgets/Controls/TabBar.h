#pragma once

#include "GUI/Widgets/Controls/Container.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// One tab strip button. Selection is owned by the parent UITabBar; the
/// button reports clicks and keyboard navigation requests.
struct YA_GUI_API UITabButton : public UIElement
{
    YA_REFLECT_BEGIN(UITabButton, UIElement)
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_FIELD(_bSelected, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_textColor, .instanceEditable())
    YA_REFLECT_FIELD(_normalColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_FIELD(_selectedColor, .instanceEditable())
    YA_REFLECT_FIELD(_padding, .instanceEditable())
    YA_REFLECT_END()

    explicit UITabButton(std::string name = "TabButton") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    ~UITabButton() override
    {
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITabButton>; }

    std::string _label;
    bool        _bSelected = false;
    uint32_t    _fontSize  = 13;
    glm::vec2   _padding   = {14.0f, 6.0f};

    glm::vec4 _textColor     = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 _normalColor   = {0.10f, 0.11f, 0.13f, 1.0f};
    glm::vec4 _hoveredColor  = {0.18f, 0.20f, 0.24f, 1.0f};
    glm::vec4 _selectedColor = {0.20f, 0.34f, 0.58f, 1.0f};

    /// Fired when this tab is activated (click / Enter / Space).
    std::function<void()> _onActivated;
    /// Fired on Left / Right (delta -1/+1); the bar moves focus + selection.
    std::function<void(int delta)> _onNavigate;
    /// DockSpace drag: fired when a press crosses the drag threshold; the
    /// bar begins the tree drag session carrying dock-tab:<label>.
    std::function<void()> _onDragArmed;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; }
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    VisualFlag _bHovered{*this};
    bool      _bPressed   = false;
    glm::vec2 _pressPoint{0.0f, 0.0f};
};

/// Tab strip (imgui-demo-style page switcher, gui-app-bootstrap Phase 4).
/// Owns the selected index; addTab() wires buttons that select themselves.
/// Left / Right on a focused tab moves focus + selection (wrap-around).
struct YA_GUI_API UITabBar : public UIContainer
{
    explicit UITabBar(std::string name = "TabBar") : UIContainer(std::move(name))
    {
        setDirection(EWidgetBoxLayout::Horizontal);
        setSpacing(2.0f);
        setPadding({4.0f, 4.0f});
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITabBar>; }

    /// Add a tab; selecting it fires `onSelected(index)` through the bar.
    UITabButton* addTab(const std::string& label);

    /// Select `index` (clamped; no-op when unchanged) and fire the callback.
    void selectTab(int index);
    /// Sync selected state without firing `_onTabSelected`.
    void syncSelectedTab(int index);
    [[nodiscard]] int getSelectedIndex() const { return _selectedIndex; }

    /// Remove the tab at `index` (detaches the button, adjusts selection).
    /// Returns the removed label (empty when out of range).
    std::string removeTab(int index);

    /// DockSpace-style tab dragging: when true a press on a tab arms a drag
    /// session (6px threshold); crossing it begins a tree drag carrying the
    /// payload "dock-tab:<label>" and fires _onTabDragBegin(index).
    bool _bDraggableTabs = false;
    std::function<void(int index, const std::string& label)> _onTabDragBegin;

    std::function<void(int selectedIndex)> _onTabSelected;

    /// When the bar has no tabs, draw this placeholder text (and keep a
    /// header-sized height) so an empty zone is still a visible drop target
    /// rather than a 0-height sliver. Set empty to disable.
    std::string _emptyPlaceholder;

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    void navigate(int delta);
    std::vector<UITabButton*> _tabs;
    int                       _selectedIndex = -1;
};

} // namespace ya
