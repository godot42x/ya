#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Scroll viewport: clipping/input host for UIScrollLayout.
///
/// Contract:
///   - UIScrollLayout owns one-content-child arrangement, the cross-axis
///     stretch, content desired extent and clamped tree-local offset;
///   - the child is laid out in tree-local coordinates (content start is
///     shifted by UIScrollLayout's offset), so the existing hit walk needs no
///     point conversion; `cullsChildHits` rejects events outside the
///     viewport rect;
///   - paint clips to the viewport rect via the snapshot clip stack;
///   - wheel is consumed by the innermost scrollable viewport; when the
///     content fits (or the scroll is already at its limit) the event is
///     not consumed and bubbles outward through the tree walk.
struct YA_GUI_API UIScrollViewport : public UIElement
{
    YA_REFLECT_BEGIN(UIScrollViewport, UIElement)
    YA_REFLECT_END()

    explicit UIScrollViewport(std::string name = "ScrollViewport") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
        _scrollLayout.setOwner(*this);
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIScrollViewport>; }

    [[nodiscard]] UIScrollLayout& getScrollLayout() { return _scrollLayout; }
    [[nodiscard]] const UIScrollLayout& getScrollLayout() const { return _scrollLayout; }
    void setAxis(EScrollAxis value) { _scrollLayout.setAxis(value); }
    void setScrollOffset(float value)
    {
        _scrollLayout.setScrollOffset(value);
        markPaintDirty(); // the scrollbar thumb follows the offset
    }
    void setScrollStep(float value) { _scrollLayout.setScrollStep(value); }
    [[nodiscard]] EScrollAxis getAxis() const { return _scrollLayout.getAxis(); }
    [[nodiscard]] float getScrollOffset() const { return _scrollLayout.getScrollOffset(); }
    [[nodiscard]] float getScrollStep() const { return _scrollLayout.getScrollStep(); }

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] bool cullsChildHits(const glm::vec2& logicalPoint) const override
    {
        return !hitTestLayoutRect(logicalPoint);
    }

    // === Scrollbar ===
    /// Draw a vertical scrollbar along the right edge when the content
    /// overflows (configurable style; set _bShowScrollbar = false to hide).
    bool      _bShowScrollbar     = true;
    float     _scrollbarWidth     = 8.0f;
    glm::vec4 _scrollbarTrackColor = {0.10f, 0.11f, 0.14f, 0.9f};
    glm::vec4 _scrollbarThumbColor = {0.34f, 0.38f, 0.46f, 1.0f};

    /// Whether the content can scroll at all (after the last layout).
    [[nodiscard]] bool isScrollable() const { return _scrollLayout.isScrollable(); }
    [[nodiscard]] float getMaxScrollOffset() const { return _scrollLayout.getMaxScrollOffset(); }

  protected:
    /// Clip the content traversal to the viewport rect (GI-302: the base paint
    /// owns self rebuild/reuse; this only customizes the children context).
    void paintChildren(UIFrameBuilder& builder) override;
    /// A changed viewport rect invalidates every descendant's resolved clip (GI-304).
    void onLayoutRectChanged() override;

  private:
    UIScrollLayout _scrollLayout;
};

} // namespace ya
