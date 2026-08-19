#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <functional>

namespace ya
{

/// Split pane: interaction/paint host for UISplitLayout. The layout owns
/// orientation, ratio, limits, padding and child geometry; this widget owns
/// divider drag transient state, per-pane clipping and visual styling.
///
/// Contract:
///   - UISplitLayout arranges the first two children and owns all split
///     geometry;
///   - each pane is its own clip region: paint clips every child to its
///     arranged pane rect so content never bleeds across the divider or
///     outside the split;
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
    void setSplitRatioChangedCallback(std::function<void(float)> callback) { _onSplitRatioChanged = std::move(callback); }
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
    VisualFlag _bDraggingDivider{*this};
    VisualFlag _bHoveredDivider{*this};
    float      _dragStartRatio   = 0.0f;
    float      _dragStartPointer = 0.0f;

    /// Reactive split-ratio binding (layout-dirty). A write re-runs the tree's
    /// layout (measure + arrange) and re-paints. Dependency is registered at
    /// bind time (layout attributes are long-lived, not per-paint reads).
    void bindSplitRatio(std::shared_ptr<Reactive<float>> ref);

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    /// The pane is hoverable only for its divider strip: hitTestSelf narrows
    /// the split's own hit region to the divider rect, so a full-area rect
    /// never steals hover/cursor from an overlapping child (e.g. a toolbar
    /// button over the top padding). Pane regions are hit by their children;
    /// padding falls through to whatever sits below.
    bool hitTestSelf(const glm::vec2& logicalPoint) const override;
    void onPointerLeave() override { _bHoveredDivider = false; }
    void resetHoverState() override { onPointerLeave(); }
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

  protected:
    /// Clip every pane child to its arranged pane rect (GI-302: the base paint
    /// owns self rebuild/reuse; this only customizes the children context).
    void paintChildren(UIFrameBuilder& builder) override;

  private:
    UISplitLayout _splitLayout;
    std::shared_ptr<Reactive<float>> _splitRatioBinding;
    std::function<void(float)> _onSplitRatioChanged;
};

} // namespace ya
