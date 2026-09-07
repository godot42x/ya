#include "GUI/Widgets/Controls/CheckBox.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UICheckBox::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    node["control"] = {
        {"type", "checkBox"},
        {"checked", _bChecked},
    };
}

namespace
{

void paintCheckMark(UIFrameBuilder& builder, const Rect2D& boxRect, const glm::vec4& color)
{
    const float x = boxRect.pos.x;
    const float y = boxRect.pos.y;
    const float w = boxRect.extent.x;
    const float h = boxRect.extent.y;
    const float thickness = std::max(1.5f, w * 0.12f);
    const glm::vec2 p0{x + w * 0.22f, y + h * 0.52f};
    const glm::vec2 p1{x + w * 0.42f, y + h * 0.74f};
    const glm::vec2 p2{x + w * 0.80f, y + h * 0.26f};
    builder.addLine(p0, p1, color, thickness);
    builder.addLine(p1, p2, color, thickness);
}

} // namespace

void UICheckBox::syncContentPadding()
{
    _contentLayout.setPadding(FMargin{_boxSize + _labelSpacing, 0.0f, 0.0f, 0.0f});
}

void UICheckBox::applyAssignedLayout(const Rect2D& rect)
{
    syncContentPadding();
    UIElement::applyAssignedLayout(rect);
}

glm::vec2 UICheckBox::computeDesiredSize() const
{
    glm::vec2 measured = _contentLayout.measure(*this);
    return {measured.x, std::max(_boxSize, measured.y)};
}

std::unique_ptr<UISlot> UICheckBox::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

void UICheckBox::paintSelf(UIFrameBuilder& builder)
{
    Rect2D boxRect = _layoutRect;
    boxRect.extent = glm::vec2(_boxSize);
    boxRect.pos.y += std::max(0.0f, (_layoutRect.extent.y - _boxSize) * 0.5f);

    const FCheckBoxStyle& style = resolvedStyle();
    const FBrush& fill = resolveVisualFill(visualChrome(style),
                                           composeVisualFlags(_bHovered,
                                                              false,
                                                              false,
                                                              !isEnabledInTree(),
                                                              _bChecked,
                                                              false,
                                                              false));
    builder.addBrush(boxRect, fill);
    if (_bChecked) {
        paintCheckMark(builder, boxRect, style.checkColor);
    }
}

void UICheckBox::toggle()
{
    _bChecked = !_bChecked;
    // _bChecked is a reflect-ed bool (serialization), not a VisualFlag: mark
    // paint-dirty manually so the check mark re-paints.
    markPaintDirty();
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
