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

    [[nodiscard]] const FOverlaySlotArgs& args() const { return _args; }
    operator const FOverlaySlotArgs&() const { return _args; }

  private:
    FOverlaySlotArgs _args{};
};

[[nodiscard]] inline FBoxSlotBuilder boxSlot() { return {}; }

[[nodiscard]] inline FOverlaySlotBuilder overlaySlot() { return {}; }

/// The public layout-intent builder: `ui::layout()` starts a spec and each
/// method ADDS A CAPABILITY TO THE TYPE.
///
///     ui::layout().fill()                     // BoxCapability + CanvasCapability
///     ui::layout().anchor({0,0}, {1,0})       // CanvasCapability
///     ui::layout().grow(1.0f)                 // BoxCapability
///     ui::layout().cell(0, 1)                 // GridCapability
///
/// The capability set is part of the type, so a host can reject an unsupported
/// intent at compile time: `column[ui::layout().anchor(...) >> w]` does not
/// compile because a box host does not implement Anchor. Modifiers are
/// rvalue-qualified so each one can return a builder of the widened type.
template<EUILayoutCap Caps = EUILayoutCap::None>
class FUILayoutSpecBuilder
{
  public:
    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Fill> fill() &&
    {
        auto next          = widen<EUILayoutCap::Fill>();
        next._spec.anchorMin = {0.0f, 0.0f};
        next._spec.anchorMax = {1.0f, 1.0f};
        next._spec.sizeRule  = EUIBoxSlotSizeRule::Fill;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Anchor> anchor(glm::vec2 min, glm::vec2 max) &&
    {
        auto next            = widen<EUILayoutCap::Anchor>();
        next._spec.anchorMin = min;
        next._spec.anchorMax = max;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Grow> grow(float weight) &&
    {
        auto next          = widen<EUILayoutCap::Grow>();
        next._spec.weight   = weight;
        next._spec.sizeRule = EUIBoxSlotSizeRule::Fill;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Cell> cell(int rowValue, int columnValue) &&
    {
        auto next         = widen<EUILayoutCap::Cell>();
        next._spec.row    = rowValue;
        next._spec.column = columnValue;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Align> align(EWidgetAlignH value) &&
    {
        auto next         = widen<EUILayoutCap::Align>();
        next._spec.alignH = value;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Align> align(EWidgetAlignH h, EWidgetAlignV v) &&
    {
        auto next         = widen<EUILayoutCap::Align>();
        next._spec.alignH = h;
        next._spec.alignV = v;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Margin> margin(glm::vec2 value) &&
    {
        auto next         = widen<EUILayoutCap::Margin>();
        next._spec.margin = value;
        return next;
    }

    /// Per-edge insets (canvas hosts). Setting an edge also makes that axis
    /// stretch, so `offsets(all)` means "fill the parent minus these insets".
    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Offsets> offsets(float left, float top,
                                                                            float right, float bottom) &&
    {
        auto next          = widen<EUILayoutCap::Offsets>();
        next._spec.offsets = FMargin(left, top, right, bottom);
        return next;
    }

    /// Auto = size to content on that axis; Fixed = honour the authored size.
    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::SizeMode> widthSizeMode(EWidgetSizeMode value) &&
    {
        auto next              = widen<EUILayoutCap::SizeMode>();
        next._spec.widthSizeMode = value;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::SizeMode> heightSizeMode(EWidgetSizeMode value) &&
    {
        auto next               = widen<EUILayoutCap::SizeMode>();
        next._spec.heightSizeMode = value;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Offsets> offsets(glm::vec2 value) &&
    {
        auto next         = widen<EUILayoutCap::Offsets>();
        next._spec.offset = value;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Size> size(glm::vec2 value) &&
    {
        auto next       = widen<EUILayoutCap::Size>();
        next._spec.size = value;
        return next;
    }

    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::SizeMode> sizeMode(EWidgetSizeMode value) &&
    {
        auto next           = widen<EUILayoutCap::SizeMode>();
        next._spec.sizeMode = value;
        return next;
    }

    /// Which point of the child lands on the resolved position (canvas hosts):
    /// (0,0) top-left, (0.5,0.5) centre. Centring a child this way does not need
    /// its size to be known in advance.
    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Pivot> pivot(glm::vec2 value) &&
    {
        auto next       = widen<EUILayoutCap::Pivot>();
        next._spec.pivot = value;
        return next;
    }

    /// Size the child asks for when its size mode is Auto on that axis (canvas
    /// hosts). A zero component falls back to the measured desired size.
    [[nodiscard]] FUILayoutSpecBuilder<Caps | EUILayoutCap::Size> preferredSize(glm::vec2 value) &&
    {
        auto next              = widen<EUILayoutCap::Size>();
        next._spec.preferredSize = value;
        return next;
    }

    [[nodiscard]] const FUILayoutSpec& args() const { return _spec; }
    operator const FUILayoutSpec&() const { return _spec; }

    /// The capability set this builder's type carries.
    [[nodiscard]] static constexpr EUILayoutCap caps() { return Caps; }

  private:
    template<EUILayoutCap Add>
    [[nodiscard]] FUILayoutSpecBuilder<Caps | Add> widen() const
    {
        FUILayoutSpecBuilder<Caps | Add> next;
        next._spec      = _spec;
        next._spec.caps = Caps | Add;
        return next;
    }

    FUILayoutSpec _spec{};

    template<EUILayoutCap>
    friend class FUILayoutSpecBuilder;
};

/// Start a layout-intent spec.
[[nodiscard]] inline FUILayoutSpecBuilder<> layout() { return {}; }

/// `ui::layout().fill() >> widget` binds the freshly built spec to a child,
/// carrying the capability set in the type so the host can check it.
template<EUILayoutCap Caps, typename TChild>
[[nodiscard]] inline TUILayoutAttachment<Caps, TChild> operator>>(const FUILayoutSpecBuilder<Caps>& spec, TChild&& child)
{
    return TUILayoutAttachment<Caps, TChild>{spec.args(), std::forward<TChild>(child)};
}

} // namespace ya::ui
