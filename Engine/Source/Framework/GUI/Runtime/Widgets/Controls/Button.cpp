#include "GUI/Widgets/Controls/Button.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UIButton::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIButton::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);

    _contentLayout.arrange(*this, _layoutRect);
}

glm::vec2 UIButton::computeDesiredSize() const
{
    // Container packing always asks for the desired size; honor the explicit
    // size unless SizeToContent is enabled. With auto, size to the first
    // visible content child plus padding (Slate ContentControl).
    if (!_bAutoSize) {
        return _size;
    }
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            return _contentLayout.measure(*this);
        }
    }
    return _size;
}

void UIButton::paintSelf(UIFrameBuilder& builder)
{
    // Resolve the (possibly reactive) enabled flag first so the dependency is
    // recorded even when the button has no visible content to draw.
    const bool      bEnabled = resolvedEnabled();
    const glm::vec4 color    = _bPressed
                                   ? _pressedColor
                                   : (_bHovered
                                          ? _hoveredColor
                                          : (_bFocused ? _focusedColor : _normalColor));
    const glm::vec4 finalColor = bEnabled ? color : glm::vec4(color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, color.a);
    builder.addSprite(_layoutRect, finalColor, nullptr);
}

bool UIButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    // Keyboard events route to the focused widget regardless of the pointer
    // position (the host dispatches them with a point outside any rect).
    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            YA_CORE_INFO("UIButton '{}' activated by keyboard", _name);
            if (_onClick) {
                _onClick();
            }
            return true;
        }
        // Other keys are not the button's business: they bubble as
        // NotHandled so the app layer can route them (e.g. list navigation).
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    // Pointer-captured events must reach the widget even outside its rect;
    // everything else requires the point to be inside.
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        // Hover is owned exclusively by the enter/leave lifecycle
        // (WidgetTree::updateHovered). The press only requests focus and
        // capture; it must not write _bHovered here, or it can desync from
        // the tree's hover owner and leave a stale highlight.
        _bPressed = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false; // stray release: no press session to complete
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        // With pointer capture the release completes the click even when
        // the pointer left the widget (standard drag-release semantics).
        if (bPointInside || ctx.bViaCapture) {
            YA_CORE_INFO("UIButton '{}' clicked", _name);
            if (_onClick) {
                _onClick();
            }
        }
        return true;
    case EEvent::MouseMoved:
        // Hover is resolved by WidgetTree::updateHovered (enter/leave
        // lifecycle) after routing; consume the move without touching state.
        return true;
    default:
        return false;
    }
}

void UIButton::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
}

} // namespace ya
