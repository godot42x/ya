#pragma once

#include "Core/Api.h"
#include "GUI/Layout/UILayoutBase.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace ya
{

enum class EScrollAxis : uint8_t
{
    Vertical,
    Horizontal,
};

/// Geometry and state policy for one scrollable content child. The viewport
/// widget owns clipping/input; this layout owns desired content extent,
/// offset clamping and assigned content rect.
class YA_GUI_API UIScrollLayout final : public UILayout
{
public:
    [[nodiscard]] EScrollAxis getAxis() const { return _axis; }
    [[nodiscard]] float getScrollOffset() const { return _scrollOffset; }
    [[nodiscard]] float getScrollStep() const { return _scrollStep; }
    [[nodiscard]] float getMaxScrollOffset() const { return _maxScrollOffset; }
    [[nodiscard]] bool isScrollable() const { return _maxScrollOffset > 0.0f; }

    void setAxis(EScrollAxis value);
    void setScrollOffset(float value);
    void setScrollStep(float value);
    /// Applies the pointer wheel delta along the configured axis. Returns
    /// true only if the offset changed; callers then consume the route.
    bool scroll(const glm::vec2& wheelDelta);

    /// The viewport owns the scrolling axis extent itself; the child edge only
    /// carries cross-axis placement (fill by default, or align at desired
    /// size), so scroll content uses the content-slot contract.
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;

    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    EScrollAxis   _axis = EScrollAxis::Vertical;
    mutable float _scrollOffset = 0.0f;
    float         _scrollStep = 40.0f;
    mutable float _maxScrollOffset = 0.0f;
};

} // namespace ya
