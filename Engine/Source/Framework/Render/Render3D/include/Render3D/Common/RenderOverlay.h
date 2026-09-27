#pragma once

#include "Core/Base.h"

#include "RHI/RenderDefines.h"

#include <glm/glm.hpp>
#include <string>

namespace ya
{

/// Overlay items an owner hands to its own screen or world draw list.
///
/// These are values, not a pipeline feature: whoever records the overlay reads
/// them into `ScreenDrawList::makeText` or `WorldDrawList::makeLine`. There is no
/// central overlay pass -- the editor draws its HUD and gizmo overlays inside
/// its viewport compose, which is what makes "the editor's overlay" the
/// editor's own fact rather than a snapshot the renderer has to carry.
struct RenderOverlayText2D
{
    std::string text{};
    glm::vec2   viewPos = glm::vec2(0.0f);
    glm::vec4   color       = glm::vec4(1.0f);
    uint32_t    fontSize    = 16;
    float       depth       = 0.0f;
};

/// World-space debug line for the viewport overlay. Not screen-space Line2D.
struct RenderOverlayLine3D
{
    glm::vec3 from  = glm::vec3(0.0f);
    glm::vec3 to    = glm::vec3(0.0f);
    glm::vec4 color = glm::vec4(1.0f);
};

} // namespace ya
