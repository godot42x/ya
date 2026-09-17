#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Radio button (minimal): dot + label; selection is managed by the host
/// (like ImGui's shared int*), the control only reports presses and renders
/// its checked state.
struct YA_GUI_API UIRadioButton : public UIElement, public UIStyledWidget<UIRadioButton, FRadioButtonStyle>
{
    YA_REFLECT_BEGIN(UIRadioButton, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FRadioButtonStyle)

    explicit UIRadioButton(std::string name = "RadioButton") : UIElement(std::move(name), "radio")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIRadioButton>; }

    bool        _bChecked = false;
    std::string _label;
    uint32_t    _fontSize = 13;

    /// Fired on click / Space / Enter (the host flips the group's selection).
    std::function<void(UIRadioButton* self)> _onSelect;

    /// Changed-only setter: repaints on a real change.
    void setChecked(bool value)
    {
        if (_bChecked == value) {
            return;
        }
        _bChecked = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "radioButton"}, {"checked", _bChecked}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { onPointerLeave(); }
    void clearTransientInputState() override { _bHovered = false; }

  private:
    VisualFlag _bHovered{*this};
};

} // namespace ya
