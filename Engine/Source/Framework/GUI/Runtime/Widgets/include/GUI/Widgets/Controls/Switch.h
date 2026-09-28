#pragma once

#include "GUI/Layout/UIContentLayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIAnimation.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>

namespace ya
{

/// Animatable channel of UISwitch: the knob/track position (0 = off, 1 = on).
/// Published next to the control so any driver - the switch's own transition,
/// a gallery showcase, or a future Game UI clip player - can address it by
/// handle instead of by string.
inline constexpr TUIAnimProperty<float> kAnimSwitchProgress{"progress"};

/// Switch (toggle): the checkbox-equivalent control that animates its state
/// change by default. The value flips immediately; the KNOB travels and the
/// track colour blends while it does.
///
/// This is the framework's first default-animated control, and it is built on
/// the same primitives every other control would use - it declares its own
/// animatable property (`progress`) and drives it with `animate()`, which
/// adds a `UITween` to the widget's animator. A single clock owns the transition;
/// a mid-flight flip reverses instead of snapping.
///
/// The visual state is paint-only: layout, hit testing and the value are
/// untouched by the animation, and the widget stops ticking once it settles.
///
/// Style: reuses FCheckBoxStyle (unchecked / hovered / checked fill + accent
/// colour) under the "switch" theme key; the knob uses the accent colour.
struct YA_GUI_API UISwitch : public UIElement, public UIStyledWidget<UISwitch, FCheckBoxStyle>
{
    using SlotArgs = FContentSlotArgs;

    YA_REFLECT_BEGIN(UISwitch, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_trackSize, .instanceEditable())
    YA_REFLECT_FIELD(_labelSpacing, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FCheckBoxStyle)

    /// Default transition length. Enough to read as motion, short enough that
    /// a click never feels lagged; 0 disables the animation entirely.
    static constexpr float kDefaultTransitionSeconds = 0.12f;

    explicit UISwitch(std::string name = "Switch");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISwitch>; }

    // === Value ===
    bool _bChecked = false;
    /// Flip the value and animate the knob toward the new state (a mid-flight
    /// flip retargets the running transition).
    void setChecked(bool value);
    [[nodiscard]] bool isChecked() const { return _bChecked; }
    /// Fired on every toggle with the new state.
    std::function<void(bool bChecked)> _onChanged;

    /// Track size (logical px); the knob is derived from its height.
    glm::vec2 _trackSize    = {34.0f, 18.0f};
    float     _labelSpacing = 8.0f;

    // === Animation ===
    /// 0 = off, 1 = on; the visual position the knob/track are drawn at.
    [[nodiscard]] float getProgress() const { return _progress; }
    /// Written by the transition (and usable as a track target). Clamped.
    void setProgress(float value);
    /// Transition length in seconds; 0 makes the switch switch instantly.
    void  setTransitionSeconds(float seconds);
    [[nodiscard]] float getTransitionSeconds() const;
    /// Settle on the current state without animating (initial state).
    void settleTransition();

    // === Animatable property ("progress") ===
    [[nodiscard]] const FUIAnimPropertyTable* getAnimatableProperties() const override;

    // === UIElement ===
    void onAttached() override;
    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override
    {
        _bHovered = false;
        _bPressed = false;
    }

    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        const FMargin& padding = _contentLayout.getPadding();
        node["type"]    = "singleChild";
        node["padding"] = {{"left", padding.left},
                           {"top", padding.top},
                           {"right", padding.right},
                           {"bottom", padding.bottom}};
    }

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;

  private:
    void toggle();
    void syncContentPadding();
    [[nodiscard]] Rect2D trackRect() const;
    [[nodiscard]] Rect2D knobRect() const;

    UISingleChildLayout              _contentLayout;
    /// Handle to the tween `animate()` put on this widget's animator; the
    /// animator drives it, this is not a parallel driver.
    std::shared_ptr<UITween>         _transition;
    float                            _progress = 0.0f;
    VisualFlag                       _bHovered{*this};
    VisualFlag                       _bPressed{*this};
};

} // namespace ya
