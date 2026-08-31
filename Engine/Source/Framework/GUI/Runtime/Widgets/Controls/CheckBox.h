#pragma once

#include "GUI/Layout/UILayout.h"
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
///   - the first visible content child (label text) fills the content box to
///     the right of the check mark; indent is content padding
///     (`_boxSize + _labelSpacing`), not child geometry. With an Auto slot,
///     the desired size = box + spacing + content.
struct YA_GUI_API UICheckBox : public UIElement, public UIStyledWidget<UICheckBox, FCheckBoxStyle>
{
    YA_REFLECT_BEGIN(UICheckBox, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_boxSize, .instanceEditable())
    YA_REFLECT_FIELD(_labelSpacing, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FCheckBoxStyle)

    explicit UICheckBox(std::string name = "CheckBox") : UIElement(std::move(name), "checkbox")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
        _contentLayout.setOwner(*this);
        syncContentPadding();
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICheckBox>; }

    [[nodiscard]] UISingleChildLayout&       getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        const FMargin& padding = _contentLayout.getPadding();
        node["type"]    = "singleChild";
        node["padding"] = {{"left", padding.left},
                           {"top", padding.top},
                           {"right", padding.right},
                           {"bottom", padding.bottom}};
    }

    bool _bChecked = false;
    void setChecked(bool value)
    {
        if (_bChecked == value) return;
        _bChecked = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    [[nodiscard]] bool isChecked() const { return _bChecked; }
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
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    void toggle();
    void syncContentPadding();
    UISingleChildLayout _contentLayout;
    VisualFlag _bHovered{*this};
    VisualFlag _bPressed{*this};
};

} // namespace ya
