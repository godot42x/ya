#include "GameEditor/UI/Viewport/EditorViewportGizmoOverlay.h"

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
    if (_brush) {
        _brush->syncHost(host);
    }
}

EWidgetRouteResult EditorViewportGizmoOverlay::dispatchEvent(const Event& event,
                                                             const glm::vec2& localPoint)
{
    if (!_controller) {
        return EWidgetRouteResult::NotHandled;
    }
    const bool bInsideRect = localPoint.x >= 0.0f && localPoint.y >= 0.0f &&
                             localPoint.x < _host.extent.x && localPoint.y < _host.extent.y;
    const bool bInside = bInsideRect || wantsPointerCapture();

    switch (event.getEventType()) {
    case EEvent::MouseMoved:
        _controller->setPointer(localPoint, bInside);
        if (_brush) {
            _brush->setHover(localPoint);
            if (_brush->isStroking()) {
                _brush->updateStroke(localPoint);
                return EWidgetRouteResult::HandledExclusive;
            }
        }
        if (wantsPointerCapture()) {
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    case EEvent::MouseButtonPressed: {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        _controller->setPointer(localPoint, bInside);
        if (press.GetMouseButton() == EMouse::Left && bInside) {
            if (_brush && _brush->isEngaged() && _brush->beginStroke(localPoint)) {
                return EWidgetRouteResult::HandledExclusive;
            }
            if (_controller->beginDrag(localPoint)) {
                return EWidgetRouteResult::HandledExclusive;
            }
        }
        break;
    }
    case EEvent::MouseButtonReleased: {
        const auto& release = static_cast<const MouseButtonReleasedEvent&>(event);
        _controller->setPointer(localPoint, bInside);
        if (release.GetMouseButton() == EMouse::Left) {
            if (_brush && _brush->isStroking()) {
                _brush->endStroke();
                return EWidgetRouteResult::HandledExclusive;
            }
            if (_controller->isDragging()) {
                _controller->endDrag();
                return EWidgetRouteResult::HandledExclusive;
            }
        }
        break;
    }
    case EEvent::KeyPressed: {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (bInside && _controller->hasSelectedEntities()) {
            switch (keyEvent.getKeyCode()) {
            case EKey::K_W:
                _controller->setOperation(EEditorViewportGizmoOperation::Translate);
                break;
            case EKey::K_E:
                _controller->setOperation(EEditorViewportGizmoOperation::Rotate);
                break;
            case EKey::K_R:
                _controller->setOperation(EEditorViewportGizmoOperation::Scale);
                break;
            default:
                break;
            }
        }
        break;
    }
    default:
        break;
    }

    if (wantsPointerCapture()) {
        if (event.isInCategory(EEventCategory::Mouse) ||
            event.isInCategory(EEventCategory::MouseButton)) {
            return EWidgetRouteResult::HandledExclusive;
        }
    }

    return EWidgetRouteResult::NotHandled;
}

bool EditorViewportGizmoOverlay::wantsPointerCapture() const
{
    return (_controller && _controller->isDragging()) || (_brush && _brush->isStroking());
}

bool EditorViewportGizmoOverlay::isActive() const
{
    // A tile stroke counts as active too: the editor must keep camera
    // navigation off the mouse for as long as the brush owns the pointer.
    return (_controller && _controller->isActive()) || (_brush && _brush->isStroking());
}

} // namespace ya
