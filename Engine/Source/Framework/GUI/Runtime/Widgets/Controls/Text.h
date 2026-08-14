#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Text element rendered through the font atlas.
struct YA_GUI_API UIText : public UIElement
{
    YA_REFLECT_BEGIN(UIText, UIElement)
    YA_REFLECT_FIELD(_text, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_FIELD(_hAlign, .instanceEditable())
    YA_REFLECT_FIELD(_vAlign, .instanceEditable())
    YA_REFLECT_END()

    explicit UIText(std::string name = "Text") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIText>; }

    std::string       _text      = "Text";
    uint32_t          _fontSize  = 16;
    glm::vec4         _color     = {1.0f, 1.0f, 1.0f, 1.0f};
    EWidgetAlignH     _hAlign    = EWidgetAlignH::Left;
    EWidgetAlignV     _vAlign    = EWidgetAlignV::Top;
    // SizeToContent: set base UIElement::_bAutoSize to measure the layout
    // rect from the text (desired = text width x lineHeight).

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
};

} // namespace ya
