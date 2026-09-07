#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Full-screen popup overlay attached to the tree's Popup layer
/// (gui-app-bootstrap Phase 4).
///
/// One shared detach-safe mechanism behind popup-like surfaces.
///
/// Modal vs dim are independent:
///   - Modal (`_bModal`) captures input until the overlay is closed by
///     content actions / Esc. Outside clicks are consumed and do **not**
///     dismiss a modal. Whether the shield is painted is `_bDimBackground`.
///   - Non-modal popups dismiss on outside click / Esc. They do not dim
///     unless the app sets `_bDimBackground`.
///   - Opening takes keyboard focus; Esc always dismisses.
///   - the first visible content child is laid out through a popup-owned
///     canvas slot; the base popup places it at `_contentPos` and sizes it
///     from `_contentExtent` when set, otherwise the child's desired size.
///     Derived classes may override the slot args (e.g. centred dialogs,
///     fixed-size menus). Children are hit-tested BEFORE the overlay
///     (topmost first), so interactive content receives events first.
///
/// Lifecycle: created via make_shared, opened with open() and closed with
/// close() / dismiss. The overlay detaches itself on close.
struct YA_GUI_API UIPopupOverlay : public UIElement, public UIStyledWidget<UIPopupOverlay, FPopupStyle>
{
    YA_REFLECT_BEGIN(UIPopupOverlay, UIElement)
    YA_REFLECT_FIELD(_bModal, .instanceEditable())
    YA_REFLECT_FIELD(_bDimBackground, .instanceEditable())
    YA_REFLECT_FIELD(_contentPos, .instanceEditable())
    YA_REFLECT_FIELD(_contentExtent, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FPopupStyle)

    explicit UIPopupOverlay(std::string name = "PopupOverlay", std::string styleKey = "popup")
        : UIElement(std::move(name), std::move(styleKey))
    {
        installLayout(std::make_unique<UICanvasLayout>());
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    enum class EOverlayRole : uint8_t
    {
        Popup,
        Modal,
    };

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPopupOverlay>; }

    [[nodiscard]] EOverlayRole getRole() const { return _bModal ? EOverlayRole::Modal : EOverlayRole::Popup; }
    void setRole(EOverlayRole role) { _bModal = role == EOverlayRole::Modal; }
    [[nodiscard]] bool isModal() const { return getRole() == EOverlayRole::Modal; }

    bool    _bModal          = false;
    /// When true the shield paints `modalFill`. Independent of `_bModal`:
    /// dimming is an app choice, not the definition of modal.
    bool    _bDimBackground  = false;
    /// Content child origin in tree-local logical pixels.
    glm::vec2 _contentPos = {0.0f, 0.0f};
    /// Optional content extent for the popup-owned canvas slot. When non-zero
    /// on an axis, the base popup sizes that axis from this value (preferred
    /// size under Auto) instead of the child's desired size. Menu still
    /// overrides with fixedSize; Dialog uses the same field via preferredSize.
    glm::vec2 _contentExtent = {0.0f, 0.0f};

    /// Fired when the overlay is dismissed by shield click / Esc / close().
    std::function<void()> _onDismiss;

    /// Attach to `tree`'s Popup layer and take keyboard focus.
    void open(WidgetTree& tree);
    /// Detach + release focus + fire _onDismiss (safe to call when closed).
    void close();
    /// Same as close(); used by shield/Esc handling.
    void dismiss() { close(); }

    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override { node["type"] = "canvas"; }
    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "popupOverlay"}, {"modal", _bModal}, {"dim", _bDimBackground}};
    }
    /// A non-modal popup shield is invisible to the user: it swallows presses
    /// (dismiss) but is transparent to hover, so the menu bar item underneath
    /// keeps its hover-switch. A modal shield blocks hover and presses
    /// regardless of whether it is dimmed.
    [[nodiscard]] bool isHoverTransparent() const override { return !_bModal; }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;
    [[nodiscard]] bool assignedLayoutInputsUnchanged() const override;
    /// Content rect (first visible child) resolved by the last layout.
    [[nodiscard]] const Rect2D* contentLayoutRect() const;
    /// Resolve the popup-owned canvas slot args for the visible content child.
    /// The base popup anchors top-left at `_contentPos` and sizes from
    /// `_contentExtent` when set, otherwise the child's desired size.
    [[nodiscard]] virtual FCanvasSlotArgs resolveContentSlotArgs(const UIElement& child) const;

  private:
    /// Self-hold while open: the overlay is created via make_shared and the
    /// tree may be its only owner; close() detaches (releasing the tree's
    /// reference) before it finishes, so the overlay must keep itself alive
    /// until close() returns.
    std::shared_ptr<UIElement> _selfHold;
    glm::vec2                  _appliedContentPos{};
    glm::vec2                  _appliedContentExtent{};
};

} // namespace ya
