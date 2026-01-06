
#pragma once

#include "Math/GLM.h"



namespace ya
{


struct FMath
{

    // use these functions to avoid Left-Handed and Right-Handed system confusion, Zero-to-One or Negative-One-to-One clip space etc.
    static glm::mat4 lookAt(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up)
    {
        return glm::lookAt(eye, center, up);
    }
    static glm::mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane)
    {
        return glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
    }
    static glm::mat4 orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        return glm::ortho(left, right, bottom, top, nearPlane, farPlane);
    }
};

} // namespace ya