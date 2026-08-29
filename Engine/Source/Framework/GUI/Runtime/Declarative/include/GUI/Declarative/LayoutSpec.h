#pragma once

// ============================================================================
// FUILayoutSpec - the public layout-intent value produced by ui::layout().
//
// A child never authors its own geometry. Layout intent is a single value on
// the parent->child edge:
//
//     parent[ui::layout().fill()               >> widget]
//     parent[ui::layout().anchor({0,0},{1,0})  >> widget]
//     parent[ui::layout().grow(1)              >> widget]
//
// The spec is a CAPABILITY SET, not a per-widget type: it carries every
// capability a host might understand (fill / grow / anchor / cell / align /
// margin / offsets / size mode). Each host consumes the capabilities it
// implements and reports the ones it does not, so intent can never be silently
// dropped the way "child authors anchors" was.
//
// This is the shared currency of the layout-unified model: DSL, WidgetTree
// attach, and the UI Designer all produce the same value.
// ============================================================================

#include "GUI/Layout/UILayout.h"
#include "GUI/Layout/UILayoutIntent.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

namespace ya
{

/// Capability bits: which intents this spec actually sets.
enum class EUILayoutCap : uint32_t
{
    None     = 0,
    Fill     = 1u << 0, // stretch to the parent on both axes
    Grow     = 1u << 1, // share the parent's remaining main-axis space
    Anchor   = 1u << 2, // anchor Min/Max rect (canvas hosts)
    Cell     = 1u << 3, // grid row/column (table hosts)
    Align    = 1u << 4, // cross-axis alignment
    Margin   = 1u << 5, // outer spacing around the child
    Offsets  = 1u << 6, // position offset from the resolved anchor/origin
    SizeMode = 1u << 7, // Fixed vs Auto size resolution
    Size     = 1u << 8, // explicit size
    Pivot    = 1u << 9, // which point of the child lands on the resolved position
};

constexpr EUILayoutCap operator|(EUILayoutCap a, EUILayoutCap b)
{
    return static_cast<EUILayoutCap>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr EUILayoutCap operator&(EUILayoutCap a, EUILayoutCap b)
{
    return static_cast<EUILayoutCap>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr EUILayoutCap& operator|=(EUILayoutCap& a, EUILayoutCap b) { return a = (a | b); }
constexpr EUILayoutCap  operator~(EUILayoutCap a)
{
    return static_cast<EUILayoutCap>(~static_cast<uint32_t>(a));
}
constexpr bool hasCap(EUILayoutCap caps, EUILayoutCap cap)
{
    return static_cast<uint32_t>(caps & cap) != 0u;
}

/// Every capability; used as the permissive default for hosts that do not
/// declare a restricted set.
inline constexpr EUILayoutCap kAllLayoutCaps = static_cast<EUILayoutCap>(0xFFFFu);

// —— Host capability sets ——
//
// A host declares which capabilities it understands. A spec carrying anything
// outside that set is rejected at compile time, so "intent silently dropped"
// cannot happen: `column[ui::layout().anchor(...) >> w]` does not compile.

/// Box hosts (row / column / container): share space along the main axis.
inline constexpr EUILayoutCap kBoxHostCaps =
    EUILayoutCap::Fill | EUILayoutCap::Grow | EUILayoutCap::Align | EUILayoutCap::Margin |
    EUILayoutCap::Size | EUILayoutCap::SizeMode;

/// Canvas hosts (panel / canvas): position by anchor rects and edge insets.
inline constexpr EUILayoutCap kCanvasHostCaps =
    EUILayoutCap::Fill | EUILayoutCap::Anchor | EUILayoutCap::Offsets | EUILayoutCap::Align |
    EUILayoutCap::Margin | EUILayoutCap::Size | EUILayoutCap::SizeMode | EUILayoutCap::Pivot;

/// Grid hosts: place a child in a cell.
inline constexpr EUILayoutCap kGridHostCaps =
    EUILayoutCap::Cell | EUILayoutCap::Align | EUILayoutCap::Margin | EUILayoutCap::Size |
    EUILayoutCap::SizeMode;

/// Single-child hosts (scroll viewport / size box / button ...): the child fills
/// the host, optionally with spacing.
inline constexpr EUILayoutCap kSingleChildHostCaps =
    EUILayoutCap::Fill | EUILayoutCap::Align | EUILayoutCap::Margin | EUILayoutCap::Size |
    EUILayoutCap::SizeMode;

/// Split hosts: two panes, positioned by ratio.
inline constexpr EUILayoutCap kSplitHostCaps =
    EUILayoutCap::Fill | EUILayoutCap::Align | EUILayoutCap::Margin | EUILayoutCap::Size |
    EUILayoutCap::SizeMode;

/// Overlay hosts: layered children with alignment.
inline constexpr EUILayoutCap kOverlayHostCaps =
    EUILayoutCap::Fill | EUILayoutCap::Align | EUILayoutCap::Margin | EUILayoutCap::Size |
    EUILayoutCap::SizeMode;

/// True when every capability in `caps` is allowed by `allowed`.
template<EUILayoutCap Caps, EUILayoutCap Allowed>
concept LayoutCapsCompatible = ((Caps & ~Allowed) == EUILayoutCap::None);

/// Resolve a builder's declared capability set; builders that do not declare one
/// default to permissive until they are migrated.
template<typename TBuilder>
constexpr EUILayoutCap allowedLayoutCaps()
{
    if constexpr (requires { TBuilder::kAllowedLayoutCaps; }) {
        return TBuilder::kAllowedLayoutCaps;
    }
    else {
        return kAllLayoutCaps;
    }
}

/// The layout intent carried by one parent->child edge (construct time).
struct FUILayoutSpec
{
    // —— canvas capability ——
    glm::vec2 anchorMin = {0.0f, 0.0f};
    glm::vec2 anchorMax = {0.0f, 0.0f};

    // —— box capability ——
    float                weight   = 1.0f; // 0 = fixed, >0 = share remaining space
    EUIBoxSlotSizeRule   sizeRule = EUIBoxSlotSizeRule::Auto;

    // —— shared capabilities ——
    glm::vec2             offset = {0.0f, 0.0f};
    glm::vec2             margin = {0.0f, 0.0f};
    FMargin               offsets{};
    EWidgetSizeMode       widthSizeMode  = EWidgetSizeMode::Fixed;
    EWidgetSizeMode       heightSizeMode = EWidgetSizeMode::Fixed;
    glm::vec2             pivot          = {0.0f, 0.0f};
    glm::vec2             preferredSize  = {0.0f, 0.0f};
    glm::vec2             minSize = {0.0f, 0.0f};
    glm::vec2             maxSize = {std::numeric_limits<float>::max(),
                                     std::numeric_limits<float>::max()};
    glm::vec2             size    = {0.0f, 0.0f};
    EWidgetSizeMode       sizeMode = EWidgetSizeMode::Fixed;
    EWidgetAlignH         alignH   = EWidgetAlignH::Left;
    EWidgetAlignV         alignV   = EWidgetAlignV::Top;
    int                   row      = 0;    // table capability
    int                   column   = 0;    // table capability

    EUILayoutCap caps = EUILayoutCap::None;

    [[nodiscard]] bool has(EUILayoutCap cap) const { return hasCap(caps, cap); }

    /// Convert to the canvas host's typed slot args.
    [[nodiscard]] FCanvasSlotArgs toCanvasArgs() const
    {
        FCanvasSlotArgs args;
        args.anchorMin = anchorMin;
        args.anchorMax = anchorMax;
        args.offset    = offset;
        args.minSize   = minSize;
        args.maxSize   = maxSize;
        args.offsets   = offsets;
        args.alignmentH     = alignH;
        args.alignmentV     = alignV;
        args.widthSizeMode  = widthSizeMode;
        args.heightSizeMode = heightSizeMode;
        args.pivot          = pivot;
        args.preferredSize  = preferredSize;
        return args;
    }
};

/// Host resolution for one parent->child edge: apply whatever capabilities the
/// host's slot type actually implements. Shared by the DSL, ui::build() and the
/// UI Designer so all three agree.
inline void applyLayoutSpecToSlot(UISlot& slot, UIElement& child, const FUILayoutSpec& spec)
{
    (void)child;
    if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
        canvas->apply(spec.toCanvasArgs());
        return;
    }
    if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
        FBoxSlotArgs args;
        if (spec.has(EUILayoutCap::Grow) || spec.has(EUILayoutCap::Fill)) {
            args.sizeRule = EUIBoxSlotSizeRule::Fill;
            args.weight   = spec.has(EUILayoutCap::Grow) ? spec.weight : 1.0f;
        }
        else {
            args.sizeRule = spec.sizeRule;
            args.weight   = spec.weight;
        }
        if (spec.has(EUILayoutCap::Margin)) {
            args.margin = FMargin::hv(spec.margin);
        }
        if (spec.has(EUILayoutCap::Align)) {
            args.crossAlignment = (spec.alignH == EWidgetAlignH::Center)
                                      ? EUIBoxSlotCrossAlignment::Center
                                      : EUIBoxSlotCrossAlignment::Stretch;
        }
        box->apply(args);
        // `size()` is a real capability, not a no-op: honour it on the child so
        // the intent is never silently dropped.
        if (spec.has(EUILayoutCap::Size)) {
            child.setSize(spec.size);
        }
        return;
    }
    if (auto* table = dynamic_cast<UITableSlot*>(&slot)) {
        if (spec.has(EUILayoutCap::Cell)) {
            table->setCell(spec.row, spec.column);
        }
    }
}

} // namespace ya
