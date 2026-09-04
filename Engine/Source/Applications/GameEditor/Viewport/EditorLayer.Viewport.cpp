#include "GameEditor/EditorLayerInternal.h"

namespace ya
{
std::vector<RenderOverlayText2D> EditorLayer::buildViewportCameraOverlayTexts() const
{
    if (!_bShowViewportCameraOverlay) {
        return {};
    }

    const glm::quat rotQuat  = glm::quat(glm::radians(_camera._rotation));
    const glm::vec3 forward  = rotQuat * FMath::Vector::WorldForward;
    const glm::vec3 position = _camera.getPosition();

    std::vector<RenderOverlayText2D> texts;
    texts.reserve(2);
    texts.push_back(RenderOverlayText2D{
        .text        = std::format("Pos {:+.2f} {:+.2f} {:+.2f}", position.x, position.y, position.z),
        .viewportPos = {kViewportCameraOverlayMarginX, kViewportCameraOverlayMarginY},
        .color       = {0.92f, 0.92f, 0.92f, 0.92f},
        .fontSize    = 16,
        .depth       = 0.0f,
    });
    texts.push_back(RenderOverlayText2D{
        .text        = std::format("Dir {:+.2f} {:+.2f} {:+.2f}", forward.x, forward.y, forward.z),
        .viewportPos = {kViewportCameraOverlayMarginX, kViewportCameraOverlayMarginY + 18.0f + kViewportCameraOverlayLineSpacing},
        .color       = {0.75f, 0.86f, 1.0f, 0.92f},
        .fontSize    = 16,
        .depth       = 0.0f,
    });
    return texts;
}



bool EditorLayer::screenToViewport(float screenX, float screenY, float& outX, float& outY) const
{
    // Check if point is within viewport bounds
    if (screenX < _viewportBounds[0].x || screenX > _viewportBounds[1].x ||
        screenY < _viewportBounds[0].y || screenY > _viewportBounds[1].y)
    {
        return false;
    }

    // Transform to viewport-local coordinates (0,0 at top-left of viewport)
    outX = screenX - _viewportBounds[0].x;
    outY = screenY - _viewportBounds[0].y;

    return true;
}

bool EditorLayer::screenToViewport(const glm::vec2 in, glm::vec2& out) const
{
    return screenToViewport(in.x, in.y, out.x, out.y);
}

} // namespace ya
