#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's own view state for the current tick: clock and viewport geometry,
/// plus the camera of the primary view. The host owns the geometry; the camera
/// is the one the producer that declared the primary view supplied, kept here so
/// the camera packet, offscreen resizes and the automation surface read one copy.
/// One per App - not a per-View packet and not present/swapchain state.
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
