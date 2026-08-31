#pragma once

#include "Core/Common/AssetRef.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Flat panel: solid color and/or image, optional 9-slice border.
struct YA_GUI_API UIPanel : public UIElement, public UIStyledWidget<UIPanel, FPanelStyle>
{
    YA_REFLECT_BEGIN(UIPanel, UIElement)
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_FIELD(_image, .instanceEditable())
    YA_REFLECT_FIELD(_bNineSlice, .instanceEditable())
    YA_REFLECT_FIELD(_nineSliceBorder, .instanceEditable())
    YA_REFLECT_FIELD(_cornerRadius, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FPanelStyle)

    explicit UIPanel(std::string name = "Panel") : UIElement(std::move(name), "panel")
    {
        // A panel is the default visual carrier of the canvas layout. Canvas is
        // a LAYOUT, not a panel feature: any element can install it via
        // installLayout(); UIPanel simply does so by default.
        installLayout(std::make_unique<UICanvasLayout>());
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPanel>; }

    /// Runtime mutable fill color (GI-202): highlight/selection presenters
    /// change this per frame, so it is a protected backing field with a
    /// changed-only setter and a getter.
  protected:
    glm::vec4 _color = {0.2f, 0.2f, 0.2f, 0.8f};
  public:
    // Authoring-only (GI-202 exception list): set once at construction /
    // deserialization; no runtime business write path yet. To be encapsulated
    // when they gain a setter.
    TextureRef _image;
    bool       _bNineSlice      = false;
    glm::vec4  _nineSliceBorder = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels
    /// Corner radius in logical px. 0 = sharp rectangle (default). Applies to the
    /// panel fill regardless of whether it is a solid color, image, or theme
    /// brush; the compose pass routes the SDF round-rect shader branch.
    float _cornerRadius = 0.0f;

    /// Overlay fillColor on the theme; keeps `_color` in sync for getColor /
    /// GI-202. Paint-only so presenters can recolor every frame without a
    /// layout pass. FPanelStyle currently has only fillColor, so this is also
    /// a complete freeze of that type.
    void setColor(const glm::vec4& value)
    {
        if (_color == value) {
            return;
        }
        _color = value;
        setStyleField("fillColor", FBrush::solid(value), EUIPropertyImpact::Paint);
    }
    [[nodiscard]] const glm::vec4& getColor() const { return _color; }
    [[nodiscard]] bool hasExplicitFill() const { return hasAuthoredStyle(); }

    void deserializeFields(const nlohmann::json& fields) override;

    /// Corner radius setter (changed-only). Routes the panel fill through the
    /// SDF round-rect shader branch when radius > 0.
    void setCornerRadius(float value)
    {
        if (_cornerRadius == value) {
            return;
        }
        _cornerRadius = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    [[nodiscard]] float getCornerRadius() const { return _cornerRadius; }

    void paintSelf(UIFrameBuilder& builder) override;

    // —— Canvas layout host ——
    // Children are positioned by anchor rects (the historical "path-B"
    // behaviour), now expressed on the canvas slot edge.
    [[nodiscard]] UICanvasLayout* getCanvasLayout() const
    {
        return dynamic_cast<UICanvasLayout*>(getLayout());
    }
    [[nodiscard]] UICanvasSlot* getCanvasSlot(const UIElement& child) const
    {
        if (UISlot* edge = getSlotForChild(child)) {
            return edge->as<UICanvasSlot>();
        }
        return nullptr;
    }
};

} // namespace ya
