#include "GUI/Widgets/Controls/PopupOverlay.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace ya
{

namespace
{

/// Overlays closed from inside their own callbacks (menu item action, modal
/// button) must outlive the unwind of that callback: close() detaches the
/// tree reference and the overlay may be the only owner of the callbacks'
/// widgets. Retired overlays are kept alive here until the next open()
/// (bounded, so a long-lived app cannot accumulate them).
std::vector<std::shared_ptr<UIElement>> g_retiredOverlays;

void retireOverlay(std::shared_ptr<UIElement>&& overlay)
{
    g_retiredOverlays.push_back(std::move(overlay));
    if (g_retiredOverlays.size() > 64) {
        g_retiredOverlays.clear();
    }
}

} // namespace

void UIPopupOverlay::open(WidgetTree& tree)
{
    if (isAttached()) {
        return;
    }
    g_retiredOverlays.clear(); // previous overlays are done unwinding
    _selfHold = shared_from_this();
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const WidgetAttachment attachment =
        tree.attachToLayer(WidgetTree::ELayer::Popup, shared_from_this(), fillArgs);
    if (!attachment.valid()) {
        return;
    }
    tree.setFocus(this);
}

void UIPopupOverlay::close()
{
    WidgetTree*    tree      = getTree();
    const auto     onDismiss = std::move(_onDismiss);
    _onDismiss               = nullptr;
    if (tree) {
        if (tree->getFocused() == this) {
            tree->setFocus(nullptr);
        }
        tree->detach(*this); // may release the tree's only reference
    }
    // Keep the overlay (and the callback widgets it owns) alive until the
    // caller's stack has unwound; this is the last statement touching `this`.
    retireOverlay(std::move(_selfHold));
    if (onDismiss) {
        onDismiss();
    }
}

void UIPopupOverlay::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect);
}

void UIPopupOverlay::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect); // full screen

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        // Policy for this edge is popup-owned; write it onto the slot before
        // the installed canvas layout arranges from that edge.
        if (UISlot* edge = getSlotForChild(*child); edge && edge->as<UICanvasSlot>()) {
            auto* slot = edge->as<UICanvasSlot>();
            slot->apply(resolveContentSlotArgs(*child));
        }
        break;
    }
    if (UILayout* layout = getLayout()) {
        layout->arrange(*this, _layoutRect);
    }
}

const Rect2D* UIPopupOverlay::contentLayoutRect() const
{
    for (const auto& child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            return &child->_layoutRect;
        }
    }
    return nullptr;
}

void UIPopupOverlay::paintSelf(UIFrameBuilder& builder)
{
    if (!_bDimBackground) {
        return;
    }
    const FPopupStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.modalFill);
}

FCanvasSlotArgs UIPopupOverlay::resolveContentSlotArgs(const UIElement& child) const
{
    (void)child;
    FCanvasSlotArgs args;
    args.offset          = _contentPos;
    args.widthSizeMode   = EWidgetSizeMode::Auto;
    args.heightSizeMode  = EWidgetSizeMode::Auto;
    if (_contentExtent.x != 0.0f || _contentExtent.y != 0.0f) {
        args.preferredSize = _contentExtent;
    }
    return args;
}

bool UIPopupOverlay::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Escape) {
            close();
            return true;
        }
        return false; // other keys bubble (NotHandled) to the app layer
    }

    // Children are hit-tested before the overlay, so a shield click here
    // means no content child consumed it.
    if (eventType == EEvent::MouseButtonPressed) {
        if (isModal()) {
            return true; // consume; modal stays until OK/Cancel/Esc
        }
        close();
        return true;
    }
    return false;
}

} // namespace ya
