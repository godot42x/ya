#pragma once

// Child intent for path-A parents, expressed at the call site.
//
// A parent that owns arrangement (box / overlay / scroll / size box / split ...)
// ignores the child's own anchors, so "fillWidth()" on the child would be dead
// code. Intent for those parents belongs to the PARENT-CHILD EDGE, which is what
// these builders construct:
//
//     ui::column("C").child(node, ui::boxSlot().fillWidth(1.0f).margin(FMargin::all(8)))
//
// Each builder is a distinct type, so the parent's child() overload picks the
// one its slot supports: an overlay cannot be given fillWidth(), a box cannot be
// given hAlign(). Unsupported intent is a compile error rather than a silent
// no-op — the property that makes this cheaper to reason about than a single
// catch-all intent struct.
//
// Builders convert to their plain args struct, so existing `child(node,
// FBoxSlotArgs{...})` call sites keep working unchanged.

#include "GUI/Layout/UILayout.h"

#include <limits>

namespace ya::ui
{

/// Explicit authoring builder for a canvas parent->child edge.
/// This is the primary public path for canvas placement.
class FCanvasSlotBuilder final
{
  public:
    FCanvasSlotBuilder& fill() & { _args.anchorMin = {0.0f, 0.0f}; _args.anchorMax = {1.0f, 1.0f}; _args.widthSizeMode = EWidgetSizeMode::Fixed; _args.heightSizeMode = EWidgetSizeMode::Fixed; return *this; }
    FCanvasSlotBuilder&& fill() && { fill(); return std::move(*this); }
    FCanvasSlotBuilder& anchor(glm::vec2 min, glm::vec2 max) & { _args.anchorMin = min; _args.anchorMax = max; return *this; }
    FCanvasSlotBuilder&& anchor(glm::vec2 min, glm::vec2 max) && { _args.anchorMin = min; _args.anchorMax = max; return std::move(*this); }
    FCanvasSlotBuilder& offset(glm::vec2 value) & { _args.offset = value; return *this; }
    FCanvasSlotBuilder&& offset(glm::vec2 value) && { _args.offset = value; return std::move(*this); }
    FCanvasSlotBuilder& insets(FMargin value) & { _args.offsets = value; return *this; }
    FCanvasSlotBuilder&& insets(FMargin value) && { _args.offsets = value; return std::move(*this); }
    FCanvasSlotBuilder& insets(glm::vec2 value) & { return insets(FMargin::hv(value)); }
    FCanvasSlotBuilder&& insets(glm::vec2 value) && { _args.offsets = FMargin::hv(value); return std::move(*this); }
    FCanvasSlotBuilder& alignment(EWidgetAlignH h, EWidgetAlignV v) & { _args.alignmentH = h; _args.alignmentV = v; return *this; }
    FCanvasSlotBuilder&& alignment(EWidgetAlignH h, EWidgetAlignV v) && { _args.alignmentH = h; _args.alignmentV = v; return std::move(*this); }
    FCanvasSlotBuilder& pivot(glm::vec2 value) & { _args.pivot = value; return *this; }
    FCanvasSlotBuilder&& pivot(glm::vec2 value) && { _args.pivot = value; return std::move(*this); }
    FCanvasSlotBuilder& size(glm::vec2 value) & { _args.fixedSize = value; _args.widthSizeMode = EWidgetSizeMode::Fixed; _args.heightSizeMode = EWidgetSizeMode::Fixed; return *this; }
    FCanvasSlotBuilder&& size(glm::vec2 value) && { _args.fixedSize = value; _args.widthSizeMode = EWidgetSizeMode::Fixed; _args.heightSizeMode = EWidgetSizeMode::Fixed; return std::move(*this); }
    FCanvasSlotBuilder& preferredSize(glm::vec2 value) & { _args.preferredSize = value; return *this; }
    FCanvasSlotBuilder&& preferredSize(glm::vec2 value) && { _args.preferredSize = value; return std::move(*this); }
    FCanvasSlotBuilder& minSize(glm::vec2 value) & { _args.minSize = value; return *this; }
    FCanvasSlotBuilder&& minSize(glm::vec2 value) && { _args.minSize = value; return std::move(*this); }
    FCanvasSlotBuilder& maxSize(glm::vec2 value) & { _args.maxSize = value; return *this; }
    FCanvasSlotBuilder&& maxSize(glm::vec2 value) && { _args.maxSize = value; return std::move(*this); }
    FCanvasSlotBuilder& widthSizeMode(EWidgetSizeMode value) & { _args.widthSizeMode = value; return *this; }
    FCanvasSlotBuilder&& widthSizeMode(EWidgetSizeMode value) && { _args.widthSizeMode = value; return std::move(*this); }
    FCanvasSlotBuilder& heightSizeMode(EWidgetSizeMode value) & { _args.heightSizeMode = value; return *this; }
    FCanvasSlotBuilder&& heightSizeMode(EWidgetSizeMode value) && { _args.heightSizeMode = value; return std::move(*this); }
    [[nodiscard]] const FCanvasSlotArgs& args() const { return _args; }
    operator const FCanvasSlotArgs&() const { return _args; }
  private:
    FCanvasSlotArgs _args{};
};

/// Layout intent for a UIBoxLayout edge (row / column / container).
///
/// fillWidth/fillHeight name the axis explicitly: in a row the main axis is X,
/// in a column it is Y, and the author should not have to remember which.
class FBoxSlotBuilder final
{
  public:
    /// Fill the main axis. In a row this stretches X, in a column it stretches Y.
    FBoxSlotBuilder& fill(float weight = 1.0f) &
    {
        _args.sizeRule = EUIBoxSlotSizeRule::Fill;
        _args.weight   = weight;
        return *this;
    }
    FBoxSlotBuilder&& fill(float weight = 1.0f) &&
    {
        _args.sizeRule = EUIBoxSlotSizeRule::Fill;
        _args.weight   = weight;
        return std::move(*this);
}
    /// Fill the row's main axis (X). No-op as an intent on a column: kept
    /// because most call sites read better with the axis named, and a mismatch
    /// is caught by the box's own direction, not by the builder.
    FBoxSlotBuilder& fillWidth(float weight = 1.0f) & { return fill(weight); }
    FBoxSlotBuilder&& fillWidth(float weight = 1.0f) && { return std::move(*this).fill(weight); }

    /// Fill the column's main axis (Y).
    FBoxSlotBuilder& fillHeight(float weight = 1.0f) & { return fill(weight); }
    FBoxSlotBuilder&& fillHeight(float weight = 1.0f) && { return std::move(*this).fill(weight); }

    FBoxSlotBuilder& autoSize() &
    {
        _args.sizeRule = EUIBoxSlotSizeRule::Auto;
        return *this;
    }
    FBoxSlotBuilder&& autoSize() &&
    {
        _args.sizeRule = EUIBoxSlotSizeRule::Auto;
        return std::move(*this);
    }

    FBoxSlotBuilder& margin(FMargin value) &
    {
        _args.margin = value;
        return *this;
    }
    FBoxSlotBuilder&& margin(FMargin value) &&
    {
        _args.margin = value;
        return std::move(*this);
    }

    FBoxSlotBuilder& margin(glm::vec2 value) &
    {
        _args.margin = FMargin::hv(value);
        return *this;
    }
    FBoxSlotBuilder&& margin(glm::vec2 value) &&
    {
        _args.margin = FMargin::hv(value);
        return std::move(*this);
    }

    /// Cross-axis behaviour when the child does not fill the main axis.
    /// Defaults to Stretch, matching UIBoxSlot's default.
    FBoxSlotBuilder& crossAlign(EUIBoxSlotCrossAlignment value) &
    {
        _args.crossAlignment = value;
        return *this;
    }
    FBoxSlotBuilder&& crossAlign(EUIBoxSlotCrossAlignment value) &&
    {
        _args.crossAlignment = value;
        return std::move(*this);
    }

    /// Preferred size on an Auto box edge. A zero component still asks the
    /// child. This is the construct-time form of UIBoxSlot::setPreferredSize.
    FBoxSlotBuilder& preferredSize(glm::vec2 value) &
    {
        _args.preferredSize = value;
        return *this;
    }
    FBoxSlotBuilder&& preferredSize(glm::vec2 value) &&
    {
        _args.preferredSize = value;
        return std::move(*this);
    }

    [[nodiscard]] const FBoxSlotArgs& args() const { return _args; }
    operator const FBoxSlotArgs&() const { return _args; }

  private:
    FBoxSlotArgs _args{};
};

/// Layout intent for a UISingleChildLayout edge (scroll viewport / size box /
/// split pane / button / selectable row / check box / compound widget ...).
///
/// These parents own both axes, so the only useful intent is how the child sits
/// inside the content box: stretch it (Fill, the default) or keep its desired
/// size and place it.
class FOverlaySlotBuilder final
{
  public:
    /// Stretch both axes. Default, and the historical behaviour of every
    /// single-child host.
    FOverlaySlotBuilder& fill() &
    {
        _args.hAlign = EUIOverlayAlignment::Fill;
        _args.vAlign = EUIOverlayAlignment::Fill;
        return *this;
    }
    FOverlaySlotBuilder&& fill() &&
    {
        _args.hAlign = EUIOverlayAlignment::Fill;
        _args.vAlign = EUIOverlayAlignment::Fill;
        return std::move(*this);
    }

    /// Keep the child's desired size on both axes, placed inside the content
    /// box. Overrides any earlier fill().
    FOverlaySlotBuilder& align(EUIOverlayAlignment hAlign, EUIOverlayAlignment vAlign) &
    {
        _args.hAlign = hAlign;
        _args.vAlign = vAlign;
        return *this;
    }
    FOverlaySlotBuilder&& align(EUIOverlayAlignment hAlign, EUIOverlayAlignment vAlign) &&
    {
        _args.hAlign = hAlign;
        _args.vAlign = vAlign;
        return std::move(*this);
    }

    FOverlaySlotBuilder& hAlign(EUIOverlayAlignment value) &
    {
        _args.hAlign = value;
        return *this;
    }
    FOverlaySlotBuilder&& hAlign(EUIOverlayAlignment value) &&
    {
        _args.hAlign = value;
        return std::move(*this);
    }

    FOverlaySlotBuilder& vAlign(EUIOverlayAlignment value) &
    {
        _args.vAlign = value;
        return *this;
    }
    FOverlaySlotBuilder&& vAlign(EUIOverlayAlignment value) &&
    {
        _args.vAlign = value;
        return std::move(*this);
    }

    FOverlaySlotBuilder& inset(FMargin value) &
    {
        _args.padding = value;
        return *this;
    }
    FOverlaySlotBuilder&& inset(FMargin value) &&
    {
        _args.padding = value;
        return std::move(*this);
    }

    FOverlaySlotBuilder& inset(glm::vec2 value) &
    {
        _args.padding = FMargin::hv(value);
        return *this;
    }
    FOverlaySlotBuilder&& inset(glm::vec2 value) &&
    {
        _args.padding = FMargin::hv(value);
        return std::move(*this);
    }

    FOverlaySlotBuilder& preferredSize(glm::vec2 value) &
    {
        _args.preferredSize = value;
        return *this;
    }
    FOverlaySlotBuilder&& preferredSize(glm::vec2 value) &&
    {
        _args.preferredSize = value;
        return std::move(*this);
    }

    [[nodiscard]] const FOverlaySlotArgs& args() const { return _args; }
    operator const FOverlaySlotArgs&() const { return _args; }

  private:
    FOverlaySlotArgs _args{};
};

[[nodiscard]] inline FBoxSlotBuilder boxSlot() { return {}; }
[[nodiscard]] inline FCanvasSlotBuilder canvasSlot() { return {}; }

[[nodiscard]] inline FOverlaySlotBuilder overlaySlot() { return {}; }

} // namespace ya::ui
