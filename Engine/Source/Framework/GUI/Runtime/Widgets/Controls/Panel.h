#pragma once

#include "Core/Common/AssetRef.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Flat panel: solid color and/or image, optional 9-slice border.
struct YA_GUI_API UIPanel : public UIElement
{
    YA_REFLECT_BEGIN(UIPanel, UIElement)
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_FIELD(_image, .instanceEditable())
    YA_REFLECT_FIELD(_bNineSlice, .instanceEditable())
    YA_REFLECT_FIELD(_nineSliceBorder, .instanceEditable())
    YA_REFLECT_END()

    explicit UIPanel(std::string name = "Panel") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPanel>; }

    // Runtime mutable fill color (GI-202): highlight/selection presenters
    // change this per frame, so it is a protected backing field with a
    // changed-only setter and a getter.
  protected:
    glm::vec4 _color = {0.2f, 0.2f, 0.2f, 0.8f};
  public:
    // Authoring-only (GI-202 exception list): set once at construction /
    // deserialization; no runtime business write path yet. To be encapsulated
    // when they gain a setter.
    TextureRef _image;
    bool       _bNineSlice      = false;
    glm::vec4  _nineSliceBorder = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels

    /// Changed-only color setter (GI-105): repaint only on a real change.
    void setColor(const glm::vec4& value)
    {
        if (_color == value) {
            return;
        }
        _color = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    [[nodiscard]] const glm::vec4& getColor() const { return _color; }

    void paintSelf(UIFrameBuilder& builder) override;
};

} // namespace ya
