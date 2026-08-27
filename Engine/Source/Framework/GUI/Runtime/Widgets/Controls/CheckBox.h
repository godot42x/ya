#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Check box: box + content label slot, toggled by click, Space or Enter
/// (gui-app-bootstrap Phase 4 tool primitive).
///
/// Input semantics (same capture contract as UIButton):
///   - pointer press requests focus and starts a pointer capture session;
///     release completes the toggle;
///   - Space / Enter on the focused box toggles;
///   - the first visible content child (label text) is arranged right of the
///     box; with _bAutoSize the desired size = box + spacing + content.
struct YA_GUI_API UICheckBox : public UIElement, public UIStyledWidget<UICheckBox, FCheckBoxStyle>
{
    YA_REFLECT_BEGIN(UICheckBox, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_boxSize, .instanceEditable())
    YA_REFLECT_FIELD(_labelSpacing, .instanceEditable())
    YA_REFLECT_END()

    explicit UICheckBox(std::string name = "CheckBox") : UIElement(std::move(name), "checkbox")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICheckBox>; }

    bool _bChecked = false;
    /// Box edge length (logical px). The box is square.
    float _boxSize      = 16.0f;
    float _labelSpacing = 8.0f;

    /// Fired on every toggle with the new state.
    std::function<void(bool bChecked)> _onChanged;

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; _bPressed = false; }

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    void toggle();
    VisualFlag _bHovered{*this};
    VisualFlag _bPressed{*this};
};

} // namespace ya
