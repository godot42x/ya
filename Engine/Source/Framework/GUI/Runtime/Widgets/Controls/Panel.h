#pragma once

#include "Core/Common/AssetRef.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Flat panel: solid color and/or image, optional 9-slice border.
struct UIPanel : public UIElement
{
    YA_REFLECT_BEGIN(UIPanel, UIElement)
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_image)
    YA_REFLECT_FIELD(_bNineSlice)
    YA_REFLECT_FIELD(_nineSliceBorder)
    YA_REFLECT_END()

    explicit UIPanel(std::string name = "Panel") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPanel>; }

    glm::vec4  _color           = {0.2f, 0.2f, 0.2f, 0.8f};
    TextureRef _image;
    bool       _bNineSlice      = false;
    glm::vec4  _nineSliceBorder = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels

    void paintSelf(const WidgetPaintContext& ctx) override;
};

} // namespace ya
