#pragma once

#include "Core/Base.h"

#include "Render3D/Common/RenderRuntimeClockState.h"

#include <glm/glm.hpp>

namespace ya
{

struct AppRenderFrameState
{
    RenderRuntimeClockState clock{};
    Rect2D    viewportRect             = {};
    float     viewportFrameBufferScale = 1.0f;
    glm::mat4 view                     = glm::mat4(1.0f);
    glm::mat4 projection               = glm::mat4(1.0f);
    glm::vec3 cameraPos                = glm::vec3(0.0f);
};

} // namespace ya
