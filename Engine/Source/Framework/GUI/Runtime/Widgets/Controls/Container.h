#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Box container: arranges children left-to-right (Horizontal) or top-to-bottom
/// (Vertical) by their desired sizes, with uniform spacing and padding.
/// Children can be clipped to the content rect via _bClipChildren.
struct UIContainer : public UIElement
{
    YA_REFLECT_BEGIN(UIContainer, UIElement)
    YA_REFLECT_FIELD(_direction, .instanceEditable())
    YA_REFLECT_FIELD(_spacing, .instanceEditable())
    YA_REFLECT_FIELD(_padding, .instanceEditable())
    YA_REFLECT_FIELD(_mainAxisAlignment, .instanceEditable())
    YA_REFLECT_FIELD(_bClipChildren, .instanceEditable())
    YA_REFLECT_FIELD(_bStretchLastChild, .instanceEditable())
    YA_REFLECT_END()

    explicit UIContainer(std::string name = "Container") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIContainer>; }

    EWidgetBoxLayout        _direction         = EWidgetBoxLayout::Horizontal;
    float                   _spacing           = 4.0f;
    /// Content inset: {horizontal, vertical} padding on both sides. The
    /// content rect = layout rect shrunk by padding on each edge — the
    /// correct way to inset a full-anchor container (a full-anchor rect with
    /// a position offset would shift the whole rect out of the parent).
    glm::vec2               _padding           = {0.0f, 0.0f};
    EWidgetMainAxisAlignment _mainAxisAlignment = EWidgetMainAxisAlignment::Start;
    bool                    _bClipChildren     = false;
    /// Fill: the last participating child absorbs the remaining main-axis
    /// space instead of its desired extent. Lets a "header rows + content"
    /// panel express the content area structurally, without magic padding.
    /// Only meaningful when the container itself has a resolved (non-Auto)
    /// extent along the main axis.
    bool                    _bStretchLastChild = false;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paint(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    /// Arrange children into a box within `contentRect` (in paint order).
    void arrangeChildren(const Rect2D& contentRect);
};

} // namespace ya
