#include "GameEditor/UI/EditorViewportHost.h"

namespace ya
{

void EditorViewportOverlayHost::setOverlay(std::shared_ptr<IEditorViewportOverlay> overlay)
{
    _overlay = std::move(overlay);
    if (_overlay) {
        _overlay->syncHost(_host);
    }
}

void EditorViewportOverlayHost::clearOverlay()
{
    _overlay.reset();
}

void EditorViewportOverlayHost::syncHost(const FEditorViewportHostState& host)
{
    _host = host;
    if (_overlay) {
        _overlay->syncHost(_host);
    }
}

EWidgetRouteResult EditorViewportOverlayHost::dispatchEvent(const Event& event, const glm::vec2& localPoint)
{
    if (!_overlay) {
        return EWidgetRouteResult::NotHandled;
    }
    return _overlay->dispatchEvent(event, localPoint);
}

bool EditorViewportOverlayHost::wantsPointerCapture() const
{
    return _overlay && _overlay->wantsPointerCapture();
}

bool EditorViewportOverlayHost::isActive() const
{
    return _overlay && _overlay->isActive();
}

} // namespace ya
