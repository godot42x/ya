#pragma once

#include "GUI/Layout/UIContentLayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Selectable list / tree row (gui-app-bootstrap Phase 2).
///
/// The row is a dumb input + presentation surface: it reports a stable item
/// ID through `_onSelect` / `_onActivate` and paints `_bSelected` as given.
/// The selection set, current selection and business-object lookup belong to
/// the ToolWorkspace / presenter, never to the row. This primitive does no
/// virtualization and owns no item list.
///
/// Content lives in a single-child slot (same contract as UIButton): the
/// label's indent/fill intent is padding + UIContentSlot, not child
/// `setPosition` / `setSize`.
///
/// Input semantics (same capture contract as UIButton):
///   - pointer press selects (requests focus + pointer capture); release
///     inside completes an activation;
///   - a release whose press was a double-click ALSO fires `_onDoubleClick`,
///     so an asset browser listens to the double-click alone (a single click
///     selects but never navigates) while menu-style rows keep single-click
///     activation. The widget reports the gesture; which one "opens" is the
///     presenter's decision, mirroring TabBar's strip callback;
///   - Enter / Space on the focused row activates;
///   - detach while pressed clears all transient state.
struct YA_GUI_API UISelectableRow : public UIElement, public UIStyledWidget<UISelectableRow, FSelectableRowStyle>
{
    using SlotArgs = FContentSlotArgs;

    YA_REFLECT_BEGIN(UISelectableRow, UIElement)
    YA_REFLECT_FIELD(_itemId, .instanceEditable())
    YA_REFLECT_FIELD(_bSelected, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FSelectableRowStyle)

    explicit UISelectableRow(std::string name = "Row");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISelectableRow>; }

    [[nodiscard]] UISingleChildLayout&       getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void                                     setContentPadding(FMargin value) { _contentLayout.setPadding(value); }
    void                                     setContentPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] const FMargin&            getContentPadding() const { return _contentLayout.getPadding(); }
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        const FMargin& padding = getContentPadding();
        node["type"]    = "singleChild";
        node["padding"] = {{"left", padding.left},
                           {"top", padding.top},
                           {"right", padding.right},
                           {"bottom", padding.bottom}};
    }

    /// Stable item ID reported to the workspace (selection/activation).
    std::string _itemId;
    /// Presentation state, written by the presenter from the workspace
    /// selection. The row never flips this itself.
    bool _bSelected = false;

    /// Changed-only selection setter (GI-105): repaint on a real change. The
    /// presenter copies the workspace selection; a same-value sync is a no-op.
    void setSelected(bool value)
    {
        if (_bSelected == value) {
            return;
        }
        _bSelected = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    /// Visual drop-target feedback, written by the tree drag session.
    VisualFlag _bDropHighlighted{*this};

    void setDraggable(bool value) { _bDraggable = value; }
    void setDragPayload(std::string value) { _dragPayload = std::move(value); }
    void setDragGhostLabel(std::string value) { _dragGhostLabel = std::move(value); }
    void setOnDropHandler(std::function<void(const std::string& payload)> handler)
    {
        _onDropped = std::move(handler);
    }

    /// Enable drag initiation: press then move past a threshold starts a
    /// tree drag session with `_dragPayload` (defaults to _itemId) and a
    /// ghost labelled `_dragGhostLabel` (defaults to _itemId).
    bool        _bDraggable     = false;
    std::string _dragPayload;
    std::string _dragGhostLabel;

    std::function<void(const std::string& itemId)> _onSelect;
    std::function<void(const std::string& itemId)> _onActivate;
    /// Fired on release when the press that opened this session was a
    /// double-click (OS click count >= 2, or two presses inside the TabBar's
    /// timing/slop window). Fires alongside `_onActivate`; rows that treat
    /// opening as a double-click gesture simply leave `_onActivate` unwired.
    std::function<void(const std::string& itemId)> _onDoubleClick;
    /// Fired when this row is the drop target of a completed drag (payload
    /// = the dragged row's payload). The row stays selected as-is; the
    /// presenter owns the model mutation (e.g. reparent).
    std::function<void(const std::string& payload)> _onDropped;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    /// Whether the pointer is currently over this row (paint + test view of
    /// the transient hover flag).
    [[nodiscard]] bool isHovered() const { return _bHovered; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    friend struct FSelectableRowDragDrop;
    UISingleChildLayout _contentLayout;
    VisualFlag _bPressed{*this};
    VisualFlag _bHovered{*this};
    glm::vec2  _pressPoint{};
    /// Whether the press that opened the current session was a double-click
    /// (OS click count or the timed/slop fallback shared with the TabBar).
    bool       _bPressWasDouble = false;
    /// Timed-double bookkeeping for the fallback when the backend does not
    /// deliver a click count.
    uint64_t   _lastPressTimeMs = 0;
    glm::vec2  _lastPressPos{};
    bool       _bHasLastPress = false;
    static constexpr uint64_t kDoubleClickMs   = 400;
    static constexpr float    kDoubleClickSlop = 6.0f;
};

} // namespace ya
