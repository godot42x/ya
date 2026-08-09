#pragma once

#include "Core/Common/AssetRef.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Flat panel: solid color and/or image, optional 9-slice border.
struct UIPanel : public UIElement
{
    explicit UIPanel(std::string name = "Panel") : UIElement(std::move(name)) {}

    glm::vec4  _color           = {0.2f, 0.2f, 0.2f, 0.8f};
    TextureRef _image;
    bool       _bNineSlice      = false;
    glm::vec4  _nineSliceBorder = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels

    void paintSelf(const WidgetPaintContext& ctx) override;
};

} // namespace ya
