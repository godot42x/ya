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
///
/// Unspecified (`ui::canvasSlot()` / bare `child(node)`) is Auto/Auto at the
/// top-left: the child is visible at its desired size. `fill()` is opt-in
/// because a canvas independently places each child; defaulting every sibling
/// to fill would stack them onto the parent rect (that is overlay, not canvas).
/// Spanning `anchor()` or non-zero `insets()` promote that axis from Auto to
/// Fixed (stretch). SizeToContent inside a stretch area is explicit and must
/// come last: `fill().widthSizeMode(Auto)` / `heightSizeMode(Auto)`.
class FCanvasSlotBuilder final
{
  public:
    FCanvasSlotBuilder()
    {
        // FCanvasSlotArgs stays Fixed so `args.fixedSize = {w,h}` still works.
        // The DSL builder matches the live UICanvasSlot default (Auto/Auto).
        _args.widthSizeMode  = EWidgetSizeMode::Auto;
        _args.heightSizeMode = EWidgetSizeMode::Auto;
    }

    FCanvasSlotBuilder& fill() & { _args.anchorMin = {0.0f, 0.0f}; _args.anchorMax = {1.0f, 1.0f}; _args.widthSizeMode = EWidgetSizeMode::Fixed; _args.heightSizeMode = EWidgetSizeMode::Fixed; return *this; }
    FCanvasSlotBuilder&& fill() && { fill(); return std::move(*this); }
    FCanvasSlotBuilder& anchor(glm::vec2 min, glm::vec2 max) &
    {
        _args.anchorMin = min;
        _args.anchorMax = max;
        promoteStretchedAutoAxes();
        return *this;
    }
    FCanvasSlotBuilder&& anchor(glm::vec2 min, glm::vec2 max) && { anchor(min, max); return std::move(*this); }
    FCanvasSlotBuilder& offset(glm::vec2 value) & { _args.offset = value; return *this; }
    FCanvasSlotBuilder&& offset(glm::vec2 value) && { offset(value); return std::move(*this); }
    FCanvasSlotBuilder& insets(FMargin value) &
    {
        _args.offsets = value;
        promoteStretchedAutoAxes();
        return *this;
    }
    FCanvasSlotBuilder&& insets(FMargin value) && { insets(value); return std::move(*this); }
    FCanvasSlotBuilder& insets(glm::vec2 value) & { return insets(FMargin::hv(value)); }
    FCanvasSlotBuilder&& insets(glm::vec2 value) && { insets(FMargin::hv(value)); return std::move(*this); }
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
    void promoteStretchedAutoAxes()
    {
        if (_args.anchorMax.x != _args.anchorMin.x ||
            _args.offsets.left + _args.offsets.right != 0.0f) {
            _args.widthSizeMode = EWidgetSizeMode::Fixed;
        }
        if (_args.anchorMax.y != _args.anchorMin.y ||
            _args.offsets.top + _args.offsets.bottom != 0.0f) {
            _args.heightSizeMode = EWidgetSizeMode::Fixed;
        }
    }

    FCanvasSlotArgs _args{};
};

/// Layout intent for a UIBoxLayout edge (row / column / container).
///
/// Unspecified is Auto on the main axis (pack to desired) and Stretch on the
/// cross axis. `fill()` takes leftover main-axis space; it is not the default
/// because a column of labels/buttons would then split the parent height
/// instead of stacking. A Fill child inside an Auto-sized parent has 0 leftover
/// and collapses on that axis — give the parent a definite size (canvas
/// `fill()`, split pane, SizeBox) before using box `fill()`.
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
    FBoxSlotBuilder& minSize(glm::vec2 value) &
    {
        _args.minSize = value;
        return *this;
    }
    FBoxSlotBuilder&& minSize(glm::vec2 value) &&
    {
        _args.minSize = value;
        return std::move(*this);
    }

    FBoxSlotBuilder& maxSize(glm::vec2 value) &
    {
        _args.maxSize = value;
        return *this;
    }
    FBoxSlotBuilder&& maxSize(glm::vec2 value) &&
    {
        _args.maxSize = value;
        return std::move(*this);
    }

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

/// Layout intent for a stacked overlay edge (UIOverlay only).
///
/// Each child is independently aligned in the parent rect. Fill stretches that
/// axis; otherwise the child keeps its desired size and Start/Center/End
/// place it. Scroll / size box / split / button content uses contentSlot().
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

/// Layout intent for a content-region edge (button / selectable row / check
/// box / size box / scroll viewport / split pane / compound widget).
///
/// These parents own a content box, so the only useful intent is how the child
/// sits inside it: stretch it (Fill, the default) or keep its desired size and
/// place it. Overlay stacking uses overlaySlot().
class FContentSlotBuilder final
{
  public:
    FContentSlotBuilder& fill() &
    {
        _args.hAlign = EUIOverlayAlignment::Fill;
        _args.vAlign = EUIOverlayAlignment::Fill;
        return *this;
    }
    FContentSlotBuilder&& fill() &&
    {
        _args.hAlign = EUIOverlayAlignment::Fill;
        _args.vAlign = EUIOverlayAlignment::Fill;
        return std::move(*this);
    }

    FContentSlotBuilder& align(EUIOverlayAlignment hAlign, EUIOverlayAlignment vAlign) &
    {
        _args.hAlign = hAlign;
        _args.vAlign = vAlign;
        return *this;
    }
    FContentSlotBuilder&& align(EUIOverlayAlignment hAlign, EUIOverlayAlignment vAlign) &&
    {
        _args.hAlign = hAlign;
        _args.vAlign = vAlign;
        return std::move(*this);
    }

    FContentSlotBuilder& hAlign(EUIOverlayAlignment value) &
    {
        _args.hAlign = value;
        return *this;
    }
    FContentSlotBuilder&& hAlign(EUIOverlayAlignment value) &&
    {
        _args.hAlign = value;
        return std::move(*this);
    }

    FContentSlotBuilder& vAlign(EUIOverlayAlignment value) &
    {
        _args.vAlign = value;
        return *this;
    }
    FContentSlotBuilder&& vAlign(EUIOverlayAlignment value) &&
    {
        _args.vAlign = value;
        return std::move(*this);
    }

    FContentSlotBuilder& inset(FMargin value) &
    {
        _args.padding = value;
        return *this;
    }
    FContentSlotBuilder&& inset(FMargin value) &&
    {
        _args.padding = value;
        return std::move(*this);
    }

    FContentSlotBuilder& inset(glm::vec2 value) &
    {
        _args.padding = FMargin::hv(value);
        return *this;
    }
    FContentSlotBuilder&& inset(glm::vec2 value) &&
    {
        _args.padding = FMargin::hv(value);
        return std::move(*this);
    }

    FContentSlotBuilder& preferredSize(glm::vec2 value) &
    {
        _args.preferredSize = value;
        return *this;
    }
    FContentSlotBuilder&& preferredSize(glm::vec2 value) &&
    {
        _args.preferredSize = value;
        return std::move(*this);
    }

    [[nodiscard]] const FContentSlotArgs& args() const { return _args; }
    operator const FContentSlotArgs&() const { return _args; }

  private:
    FContentSlotArgs _args{};
};

[[nodiscard]] inline FContentSlotBuilder contentSlot() { return {}; }

} // namespace ya::ui
