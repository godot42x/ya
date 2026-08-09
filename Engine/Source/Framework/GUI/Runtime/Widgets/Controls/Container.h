#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

enum class EWidgetBoxLayout : uint8_t
{
    Horizontal,
    Vertical,
};

/// Box container: arranges children left-to-right (Horizontal) or top-to-bottom
/// (Vertical) by their desired sizes, with uniform spacing and padding.
/// Children can be clipped to the content rect via _bClipChildren.
struct UIContainer : public UIElement
{
    explicit UIContainer(std::string name = "Container") : UIElement(std::move(name)) {}

    EWidgetBoxLayout _direction     = EWidgetBoxLayout::Horizontal;
    float            _spacing       = 4.0f;
    float            _padding       = 0.0f;
    bool             _bClipChildren = false;

    void layout(const Rect2D& parentRect) override;
    void paint(const WidgetPaintContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    /// Arrange children into a box within `contentRect` (in paint order).
    void arrangeChildren(const Rect2D& contentRect);
};

} // namespace ya
