#include "GameEditor/UI/EditorViewportGizmoOverlay.h"

#include "Core/Event.h"
#include "GameEditor/EditorLayer.h"

namespace ya
{

EditorViewportGizmoOverlay::EditorViewportGizmoOverlay(EditorLayer& layer) : _layer(&layer) {}

void EditorViewportGizmoOverlay::syncHost(const FEditorViewportHostState& host)
{
    _host = host;
    if (_layer) {
        _layer->syncViewportGizmoHost(host);
    }
}

EWidgetRouteResult EditorViewportGizmoOverlay::dispatchEvent(const Event& event,
                                                             const glm::vec2& localPoint)
{
    if (!_layer) {
        return EWidgetRouteResult::NotHandled;
    }
    const bool bInside = _host.bHovered || wantsPointerCapture();

    switch (event.getEventType()) {
    case EEvent::MouseMoved:
        _layer->setViewportGizmoPointer(localPoint, bInside);
        if (isActive() || wantsPointerCapture()) {
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    case EEvent::MouseButtonPressed: {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        _layer->setViewportGizmoPointer(localPoint, bInside);
        if (press.GetMouseButton() == EMouse::Left && _host.bHovered &&
            _layer->beginViewportGizmoDrag(localPoint)) {
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    }
    case EEvent::MouseButtonReleased: {
        const auto& release = static_cast<const MouseButtonReleasedEvent&>(event);
        _layer->setViewportGizmoPointer(localPoint, bInside);
        if (release.GetMouseButton() == EMouse::Left && _layer->isViewportGizmoDragging()) {
            _layer->endViewportGizmoDrag();
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    }
    case EEvent::KeyPressed: {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!_layer->getSelections().empty()) {
            switch (keyEvent.getKeyCode()) {
            case EKey::K_W:
                _layer->setViewportGizmoOperation(EEditorViewportGizmoOperation::Translate);
                return EWidgetRouteResult::HandledExclusive;
            case EKey::K_E:
                _layer->setViewportGizmoOperation(EEditorViewportGizmoOperation::Rotate);
                return EWidgetRouteResult::HandledExclusive;
            case EKey::K_R:
                _layer->setViewportGizmoOperation(EEditorViewportGizmoOperation::Scale);
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
    return _layer && _layer->isViewportGizmoDragging();
}

bool EditorViewportGizmoOverlay::isActive() const
{
    return _layer && _layer->isGizmoActive();
}

} // namespace ya
