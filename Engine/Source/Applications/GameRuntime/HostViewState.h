#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's own view state for the current tick: clock, viewport rect and the
/// camera matrices the host derived from its camera. One per App - not a
/// per-View packet and not present/swapchain state.
struct HostViewState
{
    HostClockState clock{};
    Rect2D    viewportRect             = {};
    float     viewportFrameBufferScale = 1.0f;
    glm::mat4 view                     = glm::mat4(1.0f);
    glm::mat4 projection               = glm::mat4(1.0f);
    glm::vec3 cameraPos                = glm::vec3(0.0f);
};

} // namespace ya
