#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

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
    YA_REFLECT_FIELD(_normalColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_FIELD(_pressedColor, .instanceEditable())
    YA_REFLECT_FIELD(_focusedColor, .instanceEditable())
    YA_REFLECT_END()

    explicit UIButton(std::string name = "Button") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
        // Buttons take part in Tab traversal (focus contract, Phase 2).
        _focusPolicy = EWidgetFocusPolicy::Focusable;
        _contentLayout.setOwner(*this);
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIButton>; }

    glm::vec4 _normalColor  = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 _hoveredColor = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 _pressedColor = {0.4f, 0.4f, 0.4f, 1.0f};
    glm::vec4 _focusedColor = {0.26f, 0.52f, 0.90f, 1.0f};

    [[nodiscard]] UISingleChildLayout& getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void setContentPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] const glm::vec2& getContentPadding() const { return _contentLayout.getPadding(); }

    // Runtime-only state (not serialized)
    bool                  _bHovered = false;
    bool                  _bPressed = false;
    bool                  _bFocused = false;
    std::function<void()> _onClick;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override;
    void onFocusGained() override { _bFocused = true; }
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
};

} // namespace ya
