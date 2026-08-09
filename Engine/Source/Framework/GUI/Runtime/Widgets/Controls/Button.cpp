#include "GUI/Widgets/Controls/Button.h"

#include "Core/Log.h"

#include "GUI/Draw2D/Render2D.h"

namespace ya
{

void UIButton::paintSelf(const WidgetPaintContext& ctx)
{
    const glm::vec4 color = _bPressed
                                ? _pressedColor
                                : (_bHovered ? _hoveredColor : _normalColor);
    Render2D::makeSprite(glm::vec3(_layoutRect.pos * ctx.uiScale, 0.0f),
                         _layoutRect.extent * ctx.uiScale,
                         nullptr,
                         color);
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
