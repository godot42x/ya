#pragma once

#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Full-screen popup overlay attached to the tree's Popup layer
/// (gui-app-bootstrap Phase 4).
///
/// One shared mechanism behind menus, combo-box popups and modal dialogs:
///   - `_bModal == false` draws a transparent shield: clicks anywhere outside
///     the content child dismiss the overlay, lower layers never see them;
///   - `_bModal == true` draws a dimming shield (modal state: the whole app
///     is blocked until dismissed);
///   - Esc dismisses; opening takes keyboard focus (the overlay owns the
///     focus until closed, which matches modal semantics);
///   - the first visible content child is laid out at `_contentPos` with its
///     desired size; children are hit-tested BEFORE the overlay (topmost
///     first), so interactive content receives events first.
///
/// Lifecycle: created via make_shared, opened with open() and closed with
/// close() / dismiss. The overlay detaches itself on close.
struct UIPopupOverlay : public UIElement
{
    YA_REFLECT_BEGIN(UIPopupOverlay, UIElement)
    YA_REFLECT_FIELD(_bModal, .instanceEditable())
    YA_REFLECT_FIELD(_modalColor, .instanceEditable())
    YA_REFLECT_FIELD(_contentPos, .instanceEditable())
    YA_REFLECT_END()

    explicit UIPopupOverlay(std::string name = "PopupOverlay") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPopupOverlay>; }

    bool    _bModal     = false;
    /// Dimming shield color (only when _bModal).
    glm::vec4 _modalColor = {0.0f, 0.0f, 0.0f, 0.45f};
    /// Content child origin in tree-local logical pixels.
    glm::vec2 _contentPos = {0.0f, 0.0f};

    /// Fired when the overlay is dismissed by shield click / Esc / close().
    std::function<void()> _onDismiss;

    /// Attach to `tree`'s Popup layer and take keyboard focus.
    void open(WidgetTree& tree);
    /// Detach + release focus + fire _onDismiss (safe to call when closed).
    void close();
    /// Same as close(); used by shield/Esc handling.
    void dismiss() { close(); }

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  protected:
    /// Content rect (first visible child) resolved by the last layout.
    [[nodiscard]] const Rect2D* contentLayoutRect() const;

  private:
    /// Self-hold while open: the overlay is created via make_shared and the
    /// tree may be its only owner; close() detaches (releasing the tree's
    /// reference) before it finishes, so the overlay must keep itself alive
    /// until close() returns.
    std::shared_ptr<UIElement> _selfHold;
};

} // namespace ya
