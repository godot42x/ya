#include "GUI/Widgets/Controls/CheckBox.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

/// Approximate a check mark with two short diagonal chains of small squares
/// (axis-aligned sprites only; no rotated quads in Render2D).
void paintCheckMark(UIFrameBuilder& builder, const Rect2D& boxRect, const glm::vec4& color)
{
    const float x = boxRect.pos.x;
    const float y = boxRect.pos.y;
    const float w = boxRect.extent.x;
    const float h = boxRect.extent.y;
    const float s = w * 0.16f; // stamp size

    // Left stroke: bottom-left -> center.
    const glm::vec2 leftStart{x + s * 0.8f, y + h * 0.58f};
    const glm::vec2 leftEnd{x + w * 0.42f, y + h * 0.82f};
    const glm::vec2 leftDir = glm::normalize(leftEnd - leftStart);
    for (float t = 0.0f; t <= 1.0f; t += 0.34f) {
        const glm::vec2 p = leftStart + leftDir * glm::length(leftEnd - leftStart) * t;
        builder.addSprite(Rect2D{.pos = p - glm::vec2(s * 0.5f), .extent = glm::vec2(s)}, color, nullptr);
    }
    // Right stroke: center -> top-right.
    const glm::vec2 rightStart{leftEnd};
    const glm::vec2 rightEnd{x + w * 0.86f, y + h * 0.20f};
    const glm::vec2 rightDir = glm::normalize(rightEnd - rightStart);
    for (float t = 0.0f; t <= 1.0f; t += 0.28f) {
        const glm::vec2 p = rightStart + rightDir * glm::length(rightEnd - rightStart) * t;
        builder.addSprite(Rect2D{.pos = p - glm::vec2(s * 0.5f), .extent = glm::vec2(s)}, color, nullptr);
    }
}

} // namespace

void UICheckBox::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UICheckBox::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);

    Rect2D boxRect = _layoutRect;
    boxRect.extent = glm::vec2(_boxSize);
    boxRect.pos.y += std::max(0.0f, (_layoutRect.extent.y - _boxSize) * 0.5f);

    Rect2D contentRect = _layoutRect;
    contentRect.pos.x = boxRect.pos.x + _boxSize + _labelSpacing;
    contentRect.extent.x = std::max(0.0f, _layoutRect.pos.x + _layoutRect.extent.x - contentRect.pos.x);
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(contentRect);
        }
    }
}

glm::vec2 UICheckBox::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    glm::vec2 content{0.0f, _boxSize};
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child->computeDesiredSize();
        content.x = std::max(content.x, desired.x);
        content.y = std::max(content.y, desired.y);
    }
    return {_boxSize + _labelSpacing + content.x, std::max(_boxSize, content.y)};
}

void UICheckBox::paintSelf(UIFrameBuilder& builder)
{
    Rect2D boxRect = _layoutRect;
    boxRect.extent = glm::vec2(_boxSize);
    boxRect.pos.y += std::max(0.0f, (_layoutRect.extent.y - _boxSize) * 0.5f);

    const glm::vec4 boxColor = _bChecked ? _checkedColor : (_bHovered ? _hoveredColor : _boxColor);
    builder.addSprite(boxRect, boxColor, nullptr);
    if (_bChecked) {
        paintCheckMark(builder, boxRect, _checkColor);
    }
}

void UICheckBox::toggle()
{
    _bChecked = !_bChecked;
    if (_onChanged) {
        _onChanged(_bChecked);
    }
}

bool UICheckBox::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Space || keyEvent._keyCode == EKey::Enter)) {
            toggle();
            return true;
        }
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (bPointInside || ctx.bViaCapture) {
            toggle();
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

} // namespace ya
