#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Which axis a scroll viewport scrolls.
enum class EScrollAxis : uint8_t
{
    Vertical,
    Horizontal,
};

/// Scroll viewport: one content child scrolled inside the viewport rect
/// (gui-app-bootstrap Phase 2).
///
/// Contract:
///   - exactly one content child; the cross axis stretches to the viewport,
///     the scroll axis is the content's desired size (clamped to at least
///     the viewport size);
///   - `_scrollOffset` is tree-local logical pixels; it is clamped at layout
///     time against the content desired size, so a shrunken viewport never
///     leaves blank space past the content end;
///   - the child is laid out in tree-local coordinates (content start is
///     shifted by `-_scrollOffset`), so the existing hit walk needs no
///     point conversion; `cullsChildHits` rejects events outside the
///     viewport rect;
///   - paint clips to the viewport rect via the snapshot clip stack;
///   - wheel is consumed by the innermost scrollable viewport; when the
///     content fits (or the scroll is already at its limit) the event is
///     not consumed and bubbles outward through the tree walk.
struct UIScrollViewport : public UIElement
{
    YA_REFLECT_BEGIN(UIScrollViewport, UIElement)
    YA_REFLECT_FIELD(_axis, .instanceEditable())
    YA_REFLECT_FIELD(_scrollOffset, .instanceEditable())
    YA_REFLECT_FIELD(_scrollStep, .instanceEditable())
    YA_REFLECT_END()

    explicit UIScrollViewport(std::string name = "ScrollViewport") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIScrollViewport>; }

    EScrollAxis _axis         = EScrollAxis::Vertical;
    /// Content offset in tree-local logical pixels (0 = content start).
    float       _scrollOffset = 0.0f;
    /// Pixels scrolled per wheel notch.
    float       _scrollStep   = 40.0f;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paint(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] bool cullsChildHits(const glm::vec2& logicalPoint) const override
    {
        return !hitTestLayoutRect(logicalPoint);
    }

    /// Whether the content can scroll at all (after the last layout).
    [[nodiscard]] bool isScrollable() const { return _maxScrollOffset > 0.0f; }
    [[nodiscard]] float getMaxScrollOffset() const { return _maxScrollOffset; }

  private:
    /// Clamped content offset computed by the last layout pass.
    float _maxScrollOffset = 0.0f;
};

} // namespace ya
