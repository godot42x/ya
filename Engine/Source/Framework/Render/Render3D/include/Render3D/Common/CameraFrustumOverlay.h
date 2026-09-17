#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderOverlay.h"

#include <glm/glm.hpp>
#include <vector>

namespace ya
{

/// Compact world-space camera gizmo depth, independent of clip far.
inline constexpr float kCameraFrustumGizmoDepth = 1.25f;

/// Append a compact camera frustum gizmo (near/far rects, sides, eye to near).
/// Far plane is `visualDepth` from the eye along the FOV rays, not the
/// CameraComponent far clip. `view` / `projection` supply pose and FOV.
void appendCameraFrustumOverlayLines(std::vector<RenderOverlayLine3D>& lines,
                                     const glm::mat4&                  view,
                                     const glm::mat4&                  projection,
                                     const glm::vec4&                  color,
                                     float                             visualDepth = kCameraFrustumGizmoDepth);

} // namespace ya
