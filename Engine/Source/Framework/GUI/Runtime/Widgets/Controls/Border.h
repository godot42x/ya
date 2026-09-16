#pragma once

#include "GUI/Layout/UIContentLayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Painted rect: solid / themed fill, outline, optional corner radius.
/// Single-child content host (padding + content slot). This is not a layout:
/// canvas anchors stay on `UICanvasPanel`, box stacking on `UIContainer`,
/// stacking on `UIOverlay`. Content textures belong on `UIImage`
/// (`overlay().child(border()).child(image())`). Nine-slice chrome is an
/// `FBrush` on `FPanelStyle.fillColor`, not a widget field.
/// A card is a themed Border (`setStyleKey("panel.sidebar.card")`), not a type.
struct YA_GUI_API UIBorder : public UIElement, public UIStyledWidget<UIBorder, FPanelStyle>
{
    using SlotArgs = FContentSlotArgs;

    YA_REFLECT_BEGIN(UIBorder, UIElement)
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_FIELD(_cornerRadius, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FPanelStyle)

    glm::vec4 _color         = {0.2f, 0.2f, 0.2f, 0.8f};
    float     _cornerRadius  = 0.0f;

    explicit UIBorder(std::string name = "Border");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIBorder>; }

    void setColor(const glm::vec4& value);
    [[nodiscard]] const glm::vec4& getColor() const { return _color; }
    [[nodiscard]] bool             hasExplicitFill() const { return hasAuthoredStyle(); }

    void  setCornerRadius(float value);
    [[nodiscard]] float getCornerRadius() const { return _cornerRadius; }

    void setPadding(FMargin value) { _contentLayout.setPadding(value); }
    void setPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] const FMargin& getPadding() const { return _contentLayout.getPadding(); }

    [[nodiscard]] const FPanelStyle& resolvedStyle(ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint,
                                                   bool bTrackDependencies = true) const;

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    void                    appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        node["type"] = "border";
    }

  protected:
    UISingleChildLayout _contentLayout;

    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;
};

} // namespace ya
