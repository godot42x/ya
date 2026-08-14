#pragma once

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
struct YA_GUI_API UICheckBox : public UIElement
{
    YA_REFLECT_BEGIN(UICheckBox, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_boxSize, .instanceEditable())
    YA_REFLECT_FIELD(_boxColor, .instanceEditable())
    YA_REFLECT_FIELD(_checkedColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_FIELD(_labelSpacing, .instanceEditable())
    YA_REFLECT_END()

    explicit UICheckBox(std::string name = "CheckBox") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICheckBox>; }

    bool _bChecked = false;
    /// Box edge length (logical px). The box is square.
    float _boxSize      = 16.0f;
    float _labelSpacing = 8.0f;

    glm::vec4 _boxColor      = {0.55f, 0.60f, 0.68f, 1.0f};
    glm::vec4 _checkedColor  = {0.24f, 0.46f, 0.82f, 1.0f};
    glm::vec4 _hoveredColor  = {0.34f, 0.38f, 0.46f, 1.0f};
    glm::vec4 _checkColor    = {0.95f, 0.96f, 0.98f, 1.0f};

    /// Fired on every toggle with the new state.
    std::function<void(bool bChecked)> _onChanged;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override { _bHovered = false; _bPressed = false; }

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    void toggle();
    bool _bHovered = false;
    bool _bPressed = false;
};

} // namespace ya
