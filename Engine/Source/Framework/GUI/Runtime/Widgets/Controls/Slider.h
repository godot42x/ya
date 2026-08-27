#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Horizontal slider (gui-app-bootstrap Phase 4 tool primitive).
///
/// Value is normalized 0..1. Input semantics:
///   - pointer press places the value at the click position and starts a
///     pointer capture drag session (same contract as UISplitPane divider);
///   - Left / Right step `_step` (with Shift stepping 10x), Home / End jump;
///   - Space / Enter on the focused slider step by `_step`.
struct YA_GUI_API UISlider : public UIElement, public UIStyledWidget<UISlider, FSliderStyle>
{
    YA_REFLECT_BEGIN(UISlider, UIElement)
    YA_REFLECT_FIELD(_value, .instanceEditable())
    YA_REFLECT_FIELD(_step, .instanceEditable())
    YA_REFLECT_FIELD(_thumbSize, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FSliderStyle)

    explicit UISlider(std::string name = "Slider") : UIElement(std::move(name), "slider")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISlider>; }

    /// Normalized value in [0, 1].
    float _value = 0.5f;
    /// Keyboard step (normalized).
    float _step = 0.05f;

    glm::vec2   _thumbSize = {12.0f, 18.0f};

    /// Fired on every value change (drag / click / keyboard).
    std::function<void(float value)> _onValueChanged;

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override { _bDragging = false; }

    /// Clamp + notify. Shared by pointer and keyboard paths.
    void setValue(float value);

  private:
    /// Value from a pointer x inside the layout rect (thumb-aware).
    [[nodiscard]] float valueFromPointer(float localX) const;
    VisualFlag _bDragging{*this};
};

} // namespace ya
