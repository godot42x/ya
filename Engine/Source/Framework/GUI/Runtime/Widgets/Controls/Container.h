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
    YA_REFLECT_FIELD(_bClipChildren, .instanceEditable())
    YA_REFLECT_END()

    explicit UIContainer(std::string name = "Container") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIContainer>; }

    EWidgetBoxLayout _direction     = EWidgetBoxLayout::Horizontal;
    float            _spacing       = 4.0f;
    float            _padding       = 0.0f;
    bool             _bClipChildren = false;

    void layout(const Rect2D& parentRect) override;
    void paint(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    /// Arrange children into a box within `contentRect` (in paint order).
    void arrangeChildren(const Rect2D& contentRect);
};

} // namespace ya
