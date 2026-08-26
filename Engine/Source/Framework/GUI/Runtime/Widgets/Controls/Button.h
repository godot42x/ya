#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>

namespace ya
{

/// Button: panel style with hover/pressed/focused states (gui-app-bootstrap
/// Phase 2 focus contract).
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
struct YA_GUI_API UIButton : public UIElement
{
    YA_REFLECT_BEGIN(UIButton, UIElement)
    YA_REFLECT_END()

    explicit UIButton(std::string name = "Button") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
        // Buttons take part in Tab traversal (focus contract, Phase 2).
        _focusPolicy = EWidgetFocusPolicy::Focusable;
        _contentLayout.setOwner(*this);
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIButton>; }

    /// Theme style key (style-system Phase 2/3). paintSelf resolves
    /// FButtonStyle by this key (per-state FBrush incl. disabledFill) from
    /// the tree theme; an empty key or an absent theme falls back to the
    /// default-constructed FButtonStyle — the framework fallback. There are
    /// no bare per-state color fields anymore (Phase 3 cleanup).
    std::string _styleKey = "button";

    [[nodiscard]] UISingleChildLayout& getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void setContentPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] const glm::vec2& getContentPadding() const { return _contentLayout.getPadding(); }

    // Runtime-only state (not serialized). VisualFlag auto-marks the button
    // paint-dirty on change, so hover/pressed/focused re-paint immediately.
    VisualFlag           _bHovered{*this};
    VisualFlag           _bPressed{*this};
    VisualFlag           _bFocused{*this};
    std::function<void()> _onClick;

    /// Reactive enabled binding (paint-dirty). Disabled dims the fill color.
    void bindEnabled(std::shared_ptr<Reactive<bool>> ref) { _enabledBinding = std::move(ref); }
    [[nodiscard]] bool resolvedEnabled() const { return _enabledBinding ? _enabledBinding->get() : true; }

    void paintSelf(UIFrameBuilder& builder) override;
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
    // UISingleChildLayout. With
    // base _bAutoSize set, desired size = first visible content child's
    // desired size + padding, so a text/image label sizes the button.
    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

private:
    UISingleChildLayout _contentLayout;
    std::shared_ptr<Reactive<bool>> _enabledBinding;
};

} // namespace ya
