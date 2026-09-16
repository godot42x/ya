#include "GUI/Widgets/Controls/PopupOverlay.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

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

[[nodiscard]] bool isFillCanvasSlot(const UISlot* edge)
{
    const auto* canvas = edge ? edge->as<UICanvasSlot>() : nullptr;
    return canvas && canvas->getAnchorMin() == glm::vec2(0.0f, 0.0f) &&
           canvas->getAnchorMax() == glm::vec2(1.0f, 1.0f);
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

glm::vec2 UIPopupOverlay::fitContentPos(const WidgetTree& tree,
                                        glm::vec2         preferredTopLeft,
                                        glm::vec2         contentExtent,
                                        const Rect2D*     flipAroundAnchor)
{
    const glm::vec2 view = tree.getLogicalExtent().toVec2();
    if (view.x <= 0.0f || view.y <= 0.0f) {
        return preferredTopLeft;
    }

    if (preferredTopLeft.y + contentExtent.y > view.y && flipAroundAnchor) {
        const float aboveY = flipAroundAnchor->pos.y - contentExtent.y;
        if (aboveY >= 0.0f) {
            preferredTopLeft = {flipAroundAnchor->pos.x, aboveY};
        }
    }

    const float maxX = std::max(0.0f, view.x - contentExtent.x);
    const float maxY = std::max(0.0f, view.y - contentExtent.y);
    preferredTopLeft.x = std::clamp(preferredTopLeft.x, 0.0f, maxX);
    preferredTopLeft.y = std::clamp(preferredTopLeft.y, 0.0f, maxY);
    return preferredTopLeft;
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

void UIPopupOverlay::applyAssignedLayout(const Rect2D& rect)
{
    _appliedContentPos    = _contentPos;
    _appliedContentExtent = _contentExtent;

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        UISlot* edge = getSlotForChild(*child);
        if (isFillCanvasSlot(edge)) {
            continue;
        }
        if (edge) {
            edge->applyArgs(resolveContentSlotArgs(*child));
        }
        break;
    }
    UIElement::applyAssignedLayout(rect);
}

bool UIPopupOverlay::assignedLayoutInputsUnchanged() const
{
    return _appliedContentPos == _contentPos && _appliedContentExtent == _contentExtent;
}

const Rect2D* UIPopupOverlay::contentLayoutRect() const
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout() || isFillCanvasSlot(getSlotForChild(*child))) {
            continue;
        }
        return &child->_layoutRect;
    }
    return nullptr;
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
