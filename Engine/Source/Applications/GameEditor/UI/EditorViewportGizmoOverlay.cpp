#include "GameEditor/UI/EditorViewportGizmoOverlay.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"

namespace ya
{

EditorViewportGizmoOverlay::EditorViewportGizmoOverlay(EditorViewportGizmoController& controller)
    : _controller(&controller)
{
}

void EditorViewportGizmoOverlay::syncHost(const FEditorViewportHostState& host)
{
    _host = host;
    if (_controller) {
        _controller->syncHost(host);
    }
}

EWidgetRouteResult EditorViewportGizmoOverlay::dispatchEvent(const Event& event,
                                                             const glm::vec2& localPoint)
{
    if (!_controller) {
        return EWidgetRouteResult::NotHandled;
    }
    const bool bInside = _host.bHovered || wantsPointerCapture();

    switch (event.getEventType()) {
    case EEvent::MouseMoved:
        _controller->setPointer(localPoint, bInside);
        if (isActive() || wantsPointerCapture()) {
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    case EEvent::MouseButtonPressed: {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        _controller->setPointer(localPoint, bInside);
        if (press.GetMouseButton() == EMouse::Left && _host.bHovered &&
            _controller->beginDrag(localPoint)) {
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    }
    case EEvent::MouseButtonReleased: {
        const auto& release = static_cast<const MouseButtonReleasedEvent&>(event);
        _controller->setPointer(localPoint, bInside);
        if (release.GetMouseButton() == EMouse::Left && _controller->isDragging()) {
            _controller->endDrag();
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    }
    case EEvent::KeyPressed: {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (_controller->hasSelectedEntities()) {
            switch (keyEvent.getKeyCode()) {
            case EKey::K_W:
                _controller->setOperation(EEditorViewportGizmoOperation::Translate);
                return EWidgetRouteResult::HandledExclusive;
            case EKey::K_E:
                _controller->setOperation(EEditorViewportGizmoOperation::Rotate);
                return EWidgetRouteResult::HandledExclusive;
            case EKey::K_R:
                _controller->setOperation(EEditorViewportGizmoOperation::Scale);
                return EWidgetRouteResult::HandledExclusive;
            default:
                break;
            }
        }
        break;
    }
    default:
        break;
    }

    if (isActive() || wantsPointerCapture()) {
        if (event.isInCategory(EEventCategory::Mouse) ||
            event.isInCategory(EEventCategory::MouseButton)) {
            return EWidgetRouteResult::HandledExclusive;
        }
    }

    return EWidgetRouteResult::NotHandled;
}

bool EditorViewportGizmoOverlay::wantsPointerCapture() const
{
    return _controller && _controller->isDragging();
}

bool EditorViewportGizmoOverlay::isActive() const
{
    return _controller && _controller->isActive();
}

} // namespace ya
