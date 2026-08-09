#include "GUI/Widgets/Controls/Button.h"

#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIButton::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 color = _bPressed
                                ? _pressedColor
                                : (_bHovered ? _hoveredColor : _normalColor);
    builder.addSprite(_layoutRect, color, nullptr);
}

bool UIButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    // Pointer-captured events must reach the widget even outside its rect;
    // everything else requires the point to be inside.
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        return true;
    case EEvent::MouseButtonReleased:
        if (_bPressed) {
            _bPressed = false;
            // With pointer capture the release completes the click even when
            // the pointer left the widget (standard drag-release semantics).
            if (bPointInside || ctx.bViaCapture) {
                YA_CORE_INFO("UIButton '{}' clicked", _name);
                if (_onClick) {
                    _onClick();
                }
            }
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
