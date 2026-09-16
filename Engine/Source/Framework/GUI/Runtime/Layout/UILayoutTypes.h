#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace ya
{

/// Horizontal alignment inside a parent-owned canvas / overlay slot, and for
/// painted text. Lives with layout slots rather than the widget kernel.
enum class EWidgetAlignH : uint8_t
{
    Left,
    Center,
    Right,
};

/// Vertical alignment inside a parent-owned canvas / overlay slot, and for
/// painted text.
enum class EWidgetAlignV : uint8_t
{
    Top,
    Center,
    Bottom,
};

/// How a child resolves its own size on an axis when its rect is computed.
enum class EWidgetSizeMode : uint8_t
{
    Fixed, // honor the slot's authored size (fixedSize / preferredSize)
    Auto,  // resolve from computeDesiredSize() (SizeToContent)
};

/// How a child is placed on one axis inside a box the parent already owns.
/// Fill stretches that axis; otherwise the child keeps its desired size and
/// Start/Center/End place it. Shared by overlay stacking and content-region
/// hosts, which answer the same geometric question per axis.
enum class EUIOverlayAlignment : uint8_t
{
    Fill,
    Start,
    Center,
    End,
};

/// Four-side inset used by box/overlay slots and single-child padding.
/// `glm::vec2` overloads mean uniform horizontal / vertical (left=right, top=bottom).
/// Not an aggregate: `{x, y}` must not silently become left/top with zero right/bottom.
struct FMargin
{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;

    FMargin() = default;
    constexpr FMargin(float left_, float top_, float right_, float bottom_)
        : left(left_)
        , top(top_)
        , right(right_)
        , bottom(bottom_)
    {
    }

    [[nodiscard]] static FMargin all(float value) { return {value, value, value, value}; }
    [[nodiscard]] static FMargin hv(float horizontal, float vertical)
    {
        return {horizontal, vertical, horizontal, vertical};
    }
    [[nodiscard]] static FMargin hv(glm::vec2 value) { return hv(value.x, value.y); }

    [[nodiscard]] float horizontal() const { return left + right; }
    [[nodiscard]] float vertical() const { return top + bottom; }
    [[nodiscard]] glm::vec2 size() const { return {horizontal(), vertical()}; }
    [[nodiscard]] glm::vec2 minOffset() const { return {left, top}; }

    friend bool operator==(const FMargin&, const FMargin&) = default;
};

} // namespace ya
