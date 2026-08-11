#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// How the two panes of a split are arranged.
enum class ESplitOrientation : uint8_t
{
    Vertical,   // divider runs vertically: panes sit side by side (left/right)
    Horizontal, // divider runs horizontally: panes stack (top/bottom)
};

/// Split pane: two content children separated by a draggable divider
/// (gui-app-bootstrap Phase 2).
///
/// Contract:
///   - the first two children are the panes (extra children are not laid
///     out); each pane is assigned its rect verbatim via layoutAssigned;
///   - the divider drag session owns pointer capture for its whole duration;
///     the split ratio is control state (initialized by `_splitRatio`, kept
///     after dragging);
///   - dragging only invalidates layout: the next snapshot re-lays out both
///     panes; GPU state is never touched by the drag;
///   - no dock / tab stack / floating windows in this primitive.
struct UISplitPane : public UIElement
{
    YA_REFLECT_BEGIN(UISplitPane, UIElement)
    YA_REFLECT_FIELD(_orientation, .instanceEditable())
    YA_REFLECT_FIELD(_splitRatio, .instanceEditable())
    YA_REFLECT_FIELD(_minFirstExtent, .instanceEditable())
    YA_REFLECT_FIELD(_minSecondExtent, .instanceEditable())
    YA_REFLECT_FIELD(_dividerThickness, .instanceEditable())
    YA_REFLECT_END()

    explicit UISplitPane(std::string name = "SplitPane") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISplitPane>; }

    ESplitOrientation _orientation     = ESplitOrientation::Vertical;
    /// Fraction (0..1) of the content extent given to the first pane.
    float             _splitRatio      = 0.5f;
    float             _minFirstExtent  = 40.0f;
    float             _minSecondExtent = 40.0f;
    float             _dividerThickness = 6.0f;

    // Drag session state (runtime only, not serialized)
    bool  _bDraggingDivider = false;
    float _dragStartRatio   = 0.0f;
    float _dragStartPointer = 0.0f;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

    /// Divider rect in tree-local logical pixels (depends on the last
    /// layout + current ratio).
    [[nodiscard]] Rect2D getDividerRect() const;

  private:
    /// Clamp `_splitRatio` so both panes keep their minimum extent.
    void clampRatio(const Rect2D& contentRect);
};

} // namespace ya
