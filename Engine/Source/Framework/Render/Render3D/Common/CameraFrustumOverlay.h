#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderOverlay.h"

#include <glm/glm.hpp>
#include <vector>

namespace ya
{

/// Place a preview rect in the host viewport's bottom-right, in the host RT's
/// pixel space (origin at the RT top-left, not the chrome widget offset).
[[nodiscard]] Rect2D makeBottomRightViewInset(const glm::vec2& hostExtent,
                                              float            widthFraction  = 0.28f,
                                              float            marginFraction = 0.02f);

/// Append a world-space camera frustum (near/far rects, sides, eye to near).
/// `view` / `projection` must match the View that will be recorded.
void appendCameraFrustumOverlayLines(std::vector<RenderOverlayLine3D>& lines,
                                     const glm::mat4&                  view,
                                     const glm::mat4&                  projection,
                                     const glm::vec4&                  color);

} // namespace ya
