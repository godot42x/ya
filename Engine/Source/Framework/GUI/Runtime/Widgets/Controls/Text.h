#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Text element rendered through the font atlas.
struct UIText : public UIElement
{
    explicit UIText(std::string name = "Text") : UIElement(std::move(name)) {}

    std::string       _text      = "Text";
    uint32_t          _fontSize  = 16;
    glm::vec4         _color     = {1.0f, 1.0f, 1.0f, 1.0f};
    EWidgetAlignH     _hAlign    = EWidgetAlignH::Left;
    EWidgetAlignV     _vAlign    = EWidgetAlignV::Top;
    bool              _bAutoSize = false; // Measure the layout rect from the text

    void paintSelf(const WidgetPaintContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
};

} // namespace ya
