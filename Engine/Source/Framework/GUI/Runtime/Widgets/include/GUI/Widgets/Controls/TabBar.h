#pragma once

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Theme.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// One tab strip button. Selection is owned by the parent UITabBar; the
/// button reports clicks and keyboard navigation requests.
struct YA_GUI_API UITabButton : public UIElement, public UIStyledWidget<UITabButton, FTabStyle>
{
    YA_REFLECT_BEGIN(UITabButton, UIElement)
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_FIELD(_bSelected, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FTabStyle)

    explicit UITabButton(std::string name = "TabButton") : UIElement(std::move(name), "tab")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    ~UITabButton() override
    {
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITabButton>; }

    std::string _label;
    bool        _bSelected  = false;
    bool        _bClosable  = false;
    /// When false, a press never arms tab drag (Locked page tabs). Click still
    /// selects. Default true so dock wells stay draggable.
    bool        _bDraggable = true;
    uint32_t    _fontSize   = 13;

    /// Fired when this tab is activated (click / Enter / Space).
    std::function<void()> _onActivated;
    /// Fired on Left / Right (delta -1/+1); the bar moves focus + selection.
    std::function<void(int delta)> _onNavigate;
    /// DockSpace drag: fired when a press crosses the drag threshold; the
    /// bar begins the tree drag session carrying dock-tab:<label>.
    std::function<void()> _onDragArmed;
    /// Right-click on this tab (logical point in tree space).
    std::function<void(const glm::vec2& logicalPoint)> _onContextMenu;
    /// Fired when the close hit-zone is clicked (only if `_bClosable`).
    std::function<void()> _onClose;

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        node["control"] = {{"type", "tabButton"},
                           {"label", _label},
                           {"selected", _bSelected},
                           {"closable", _bClosable}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    /// WidgetTree auto-detect must not spawn a ghost. Tab drag starts only
    /// through `_onDragArmed` after the pointer leaves the well.
    UIDragDropOperationRef onDragDetected(const FDragDetectedEvent&) override { return nullptr; }
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override
    {
        _bHovered     = false;
        _bPressed     = false;
        _bDidReorder  = false;
    }
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] Rect2D    closeHitRect() const;

  private:
    VisualFlag _bHovered{*this};
    bool      _bPressed     = false;
    bool      _bDidReorder  = false;
    glm::vec2 _pressPoint{0.0f, 0.0f};
};

/// Tab strip (imgui-demo-style page switcher, gui-app-bootstrap Phase 4).
/// Owns the selected index; addTab() wires buttons that select themselves.
/// Left / Right on a focused tab moves focus + selection (wrap-around).
struct YA_GUI_API UITabBar : public UIContainer, public UIStyledWidget<UITabBar, FTabStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FTabStyle)

    explicit UITabBar(std::string name = "TabBar") : UIContainer(std::move(name), "tab")
    {
        setDirection(EWidgetBoxLayout::Horizontal);
        setSpacing(2.0f);
        setPadding({2.0f, 1.0f});
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITabBar>; }
    bool isHoverable() const override { return true; }

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
    void clearTabs();
    [[nodiscard]] int tabCount() const { return static_cast<int>(_tabs.size()); }
    [[nodiscard]] UITabButton* tabAt(int index)
    {
        return index >= 0 && index < tabCount() ? _tabs[static_cast<size_t>(index)] : nullptr;
    }

    /// DockSpace-style tab dragging: when true a press on a draggable tab
    /// arms a gesture. Movement that stays inside this bar only reorders;
    /// leaving the bar fires `_onTabDragBegin` (dock/tear-off).
    bool _bDraggableTabs = false;
    std::function<void(int index, const std::string& label)> _onTabDragBegin;
    /// Fired when an in-well drag swaps `from` → `to` (indices before the swap).
    std::function<void(int from, int to)> _onTabReordered;
    /// Optional extra gate after `_bDraggable`. Return false to keep the press
    /// as a click (no ghost, no tear-off).
    std::function<bool(int index)> _canBeginTabDrag;

    [[nodiscard]] int  indexOfTab(const UITabButton* button) const;
    void               reorderDraggedTab(UITabButton& button, const glm::vec2& point);
    [[nodiscard]] bool allowsArmedTabDrag(const UITabButton& button) const;

    std::function<void(int selectedIndex)> _onTabSelected;
    /// Right-click on a tab (`index`) or the strip (`index < 0`).
    std::function<void(int index, const glm::vec2& logicalPoint)> _onTabContextMenu;
    /// Double-click on the empty strip only (not a tab button). Hosts bind
    /// this to native maximize. The title TabBar is published as a Client
    /// hole so these clicks reach the widget; trailing gutter stays Drag.
    std::function<void()> _onStripDoubleClick;

    /// When the bar has no tabs, draw this placeholder text (and keep a
    /// header-sized height) so an empty zone is still a visible drop target
    /// rather than a 0-height sliver. Set empty to disable.
    std::string _emptyPlaceholder;

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    void navigate(int delta);
    void rebindTabCallbacks();
    void moveTabVisual(int from, int to);
    std::vector<UITabButton*> _tabs;
    int                       _selectedIndex = -1;
    uint64_t                  _lastEmptyPressTimeMs = 0;
    glm::vec2                 _lastEmptyPressPos{0.0f, 0.0f};
    bool                      _bHasLastEmptyPress = false;
};

} // namespace ya
