#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>

namespace ya
{

/// Button: single-child content control (Slate ContentControl). It has no
/// text property; a typical label is a UIText child constructed into the
/// content slot. Hover/pressed/focused states follow the Phase 2 focus
/// contract.
///
/// Input semantics:
///   - pointer press requests tree focus and starts a pointer capture
///     session; release completes the click (also when the pointer left the
///     widget mid-press, via capture) and ends the session;
///   - Enter / Space on the focused button activates the same _onClick
///     callback as the mouse click;
///   - detach while pressed clears all transient state (tree + widget).
/// Click callback is runtime-only (not serialized); hit testing is driven by
/// the tree walker.
struct YA_GUI_API UIButton : public UIElement, public UIStyledWidget<UIButton, FButtonStyle>
{
    YA_REFLECT_BEGIN(UIButton, UIElement)
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FButtonStyle)

    explicit UIButton(std::string name = "Button") : UIElement(std::move(name), "button")
    {
        _hitFilter = EWidgetHitFilter::Stop;
        // Buttons take part in Tab traversal (focus contract, Phase 2).
        _focusPolicy = EWidgetFocusPolicy::Focusable;
        _contentLayout.setOwner(*this);
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIButton>; }

    [[nodiscard]] UISingleChildLayout&       getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void                                     setContentPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] glm::vec2                  getContentPadding() const
    {
        const FMargin& padding = _contentLayout.getPadding();
        return {padding.left, padding.top};
    }
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        node["type"]    = "singleChild";
        node["padding"] = {{"x", getContentPadding().x}, {"y", getContentPadding().y}};
    }

    // Runtime-only state (not serialized). VisualFlag auto-marks the button
    // paint-dirty on change, so hover/pressed/focused re-paint immediately.
    VisualFlag            _bHovered{*this};
    VisualFlag            _bPressed{*this};
    VisualFlag            _bFocused{*this};
    std::function<void()> _onClick;

    /// Reactive enabled binding (paint-dirty). Disabled dims the fill color.
    void               bindEnabled(std::shared_ptr<Reactive<bool>> ref) { _enabledBinding = std::move(ref); }
    [[nodiscard]] bool resolvedEnabled() const
    {
        return isEnabledInTree() && (!_enabledBinding || _enabledBinding->get());
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { onPointerLeave(); }
    void clearTransientInputState() override;
    void onFocusGained(bool bFromKeyboard) override { _bFocused = bFromKeyboard; }
    void onFocusLost() override { _bFocused = false; }

    // Content-slot layout (Slate ContentControl model): the button resolves
    // its own rect (anchor math) and delegates its only child to
    // UISingleChildLayout. Desired size is content + padding; authored size
    // lives on the parent-owned slot.
    void                    layout(const Rect2D& parentRect) override;
    void                    layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    /// The button owns its content box via UISingleChildLayout, so the label's
    /// intent lives on the edge: fill (default) or align at desired size.
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    UISingleChildLayout             _contentLayout;
    std::shared_ptr<Reactive<bool>> _enabledBinding;
};

} // namespace ya
