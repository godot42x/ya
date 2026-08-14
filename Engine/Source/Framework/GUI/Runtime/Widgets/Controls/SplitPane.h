#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Split pane: interaction/paint host for UISplitLayout. The layout owns
/// orientation, ratio, limits, padding and child geometry; this widget owns
/// divider drag transient state and visual styling.
///
/// Contract:
///   - UISplitLayout arranges the first two children and owns all split
///     geometry;
///   - the divider drag session owns pointer capture for its whole duration;
///   - dragging only invalidates layout: the next snapshot re-lays out both
///     panes; GPU state is never touched by the drag;
///   - no dock / tab stack / floating windows in this primitive.
struct YA_GUI_API UISplitPane : public UIElement
{
    YA_REFLECT_BEGIN(UISplitPane, UIElement)
    YA_REFLECT_END()

    explicit UISplitPane(std::string name = "SplitPane") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
        _splitLayout.setOwner(*this);
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISplitPane>; }

    [[nodiscard]] UISplitLayout& getSplitLayout() { return _splitLayout; }
    [[nodiscard]] const UISplitLayout& getSplitLayout() const { return _splitLayout; }
    void setOrientation(ESplitOrientation value) { _splitLayout.setOrientation(value); }
    void setSplitRatio(float value) { _splitLayout.setSplitRatio(value); }
    void setMinFirstExtent(float value) { _splitLayout.setMinFirstExtent(value); }
    void setMinSecondExtent(float value) { _splitLayout.setMinSecondExtent(value); }
    void setDividerThickness(float value) { _splitLayout.setDividerThickness(value); }
    void setPadding(glm::vec2 value) { _splitLayout.setPadding(value); }
    [[nodiscard]] ESplitOrientation getOrientation() const { return _splitLayout.getOrientation(); }
    [[nodiscard]] float getSplitRatio() const { return _splitLayout.getSplitRatio(); }
    [[nodiscard]] float getMinFirstExtent() const { return _splitLayout.getMinFirstExtent(); }
    [[nodiscard]] float getMinSecondExtent() const { return _splitLayout.getMinSecondExtent(); }
    [[nodiscard]] float getDividerThickness() const { return _splitLayout.getDividerThickness(); }

    glm::vec4 _dividerColor          = {0.20f, 0.22f, 0.27f, 1.0f};
    glm::vec4 _dividerHoveredColor   = {0.34f, 0.40f, 0.50f, 1.0f};
    glm::vec4 _dividerDraggingColor  = {0.24f, 0.46f, 0.82f, 1.0f};

    // Drag session state (runtime only, not serialized)
    bool  _bDraggingDivider = false;
    bool  _bHoveredDivider  = false;
    float _dragStartRatio   = 0.0f;
    float _dragStartPointer = 0.0f;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void resetHoverState() override { _bHoveredDivider = false; }
    [[nodiscard]] ECursorType getCursor() const override
    {
        if (!_bHoveredDivider && !_bDraggingDivider) {
            return ECursorType::Arrow;
        }
        return _splitLayout.getOrientation() == ESplitOrientation::Vertical
                   ? ECursorType::ResizeEastWest
                   : ECursorType::ResizeNorthSouth;
    }
    void clearTransientInputState() override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

    /// Divider rect in tree-local logical pixels (depends on UISplitLayout's
    /// last arrangement).
    [[nodiscard]] Rect2D getDividerRect() const { return _splitLayout.getDividerRect(); }

private:
    UISplitLayout _splitLayout;
};

} // namespace ya
