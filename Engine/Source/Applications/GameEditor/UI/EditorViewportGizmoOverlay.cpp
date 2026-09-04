#include "GameEditor/UI/EditorViewportGizmoOverlay.h"

#include "Core/Event.h"
#include "GameEditor/EditorLayer.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace ya
{

EditorViewportGizmoOverlay::EditorViewportGizmoOverlay(EditorLayer& layer) : _layer(&layer) {}

void EditorViewportGizmoOverlay::syncHost(const FEditorViewportHostState& host)
{
    _host = host;
}

EWidgetRouteResult EditorViewportGizmoOverlay::dispatchEvent(const Event& event,
                                                             const glm::vec2& localPoint)
{
    if (!_layer || !_host.bHovered) {
        return EWidgetRouteResult::NotHandled;
    }

    _windowMousePos = _host.widgetRect.pos + localPoint;

    switch (event.getEventType()) {
        case EEvent::MouseMoved:
            break;
        case EEvent::MouseButtonPressed: {
            const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
            const int   index = static_cast<int>(press.GetMouseButton());
            if (index >= 0 && index < 3) {
                _bMouseDown[index] = true;
            }
            break;
        }
        case EEvent::MouseButtonReleased: {
            const auto& release = static_cast<const MouseButtonReleasedEvent&>(event);
            const int   index     = static_cast<int>(release.GetMouseButton());
            if (index >= 0 && index < 3) {
                _bMouseDown[index] = false;
            }
            break;
        }
        case EEvent::KeyPressed: {
            const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (!_layer->getSelections().empty()) {
                switch (keyEvent.getKeyCode()) {
                    case EKey::K_W:
                        _layer->setViewportGizmoOperation(ImGuizmo::TRANSLATE);
                        return EWidgetRouteResult::HandledExclusive;
                    case EKey::K_E:
                        _layer->setViewportGizmoOperation(ImGuizmo::ROTATE);
                        return EWidgetRouteResult::HandledExclusive;
                    case EKey::K_R:
                        _layer->setViewportGizmoOperation(ImGuizmo::SCALE);
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
    return _layer && ImGuizmo::IsUsing();
}

bool EditorViewportGizmoOverlay::isActive() const
{
    return _layer && _layer->isGizmoActive();
}

void EditorViewportGizmoOverlay::syncImGuiIO() const
{
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = {_windowMousePos.x, _windowMousePos.y};
    for (int i = 0; i < 3; ++i) {
        io.MouseDown[i] = _bMouseDown[i];
    }
}

} // namespace ya
