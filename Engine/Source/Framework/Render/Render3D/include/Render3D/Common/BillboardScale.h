#pragma once

#include "Core/Math/GLM.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ya
{

/// World-space edge length of a screen-constant billboard.
///
/// Perspective: `pixels/viewHeight * 2 * distance * tan(fovY/2)`, read from
/// `projection[1][1] == 1/tan(fovY/2)` (glm RH_ZO). Orthographic (no perspective
/// divide, `projection[2][3] == 0`): `pixels/viewHeight * visibleHeight`, and
/// `projection[1][1] == 1/halfHeight`.
[[nodiscard]] inline float billboardWorldSize(const glm::mat4& projection,
                                              float            viewHeight,
                                              float            distance,
                                              float            screenSizePixels,
                                              float            minWorldScale)
{
    const float pixels = std::max(screenSizePixels, 1.0f);
    const float p11    = projection[1][1];
    if (viewHeight <= 0.0f || std::abs(p11) <= std::numeric_limits<float>::epsilon()) {
        return std::max(minWorldScale, 0.0f);
    }

    const bool  bPerspective = std::abs(projection[2][3]) > 0.5f;
    const float span         = bPerspective
                ? (pixels / viewHeight) * 2.0f * distance / std::abs(p11)
                : (pixels / viewHeight) * 2.0f / std::abs(p11);
    return std::max(minWorldScale, span);
}

} // namespace ya
