
#pragma once

#include "Core/Math/GLM.h"



namespace ya
{


struct FMath
{
    struct Vector
    {
        // right-hand coordinate system conventions
        static constexpr bool      IsRightHanded = true;
        static constexpr glm::vec3 WorldUp       = glm::vec3(0.0f, 1.0f, 0.0f);
        static constexpr glm::vec3 WorldRight    = glm::vec3(1.0f, 0.0f, 0.0f);
        // right-handed 看向屏幕内侧
        static constexpr glm::vec3 WorldForward = glm::vec3(0.0f, 0.0f, -1.0f);

        // static constexpr glm::vec3 Axis
    };

    // use these functions to avoid Left-Handed and Right-Handed system confusion, Zero-to-One or Negative-One-to-One clip space etc.
    static glm::mat4 lookAt(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up)
    {
        return glm::lookAtRH(eye, center, up);
    }
    static glm::mat4 perspective(float fovyInRadians, float aspect, float nearPlane, float farPlane)
    {
        return glm::perspectiveRH_ZO(fovyInRadians, aspect, nearPlane, farPlane);
    }
    static glm::mat4 orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        return glm::orthoRH_ZO(left, right, bottom, top, nearPlane, farPlane);
    }

    static glm::mat4 calcViewFrom(glm::vec3 pos, glm::vec3 rotDegrees)
    {
        const glm::quat rotQuat = glm::quat(glm::radians(rotDegrees));

        // viewMatrix = glm::translate(glm::mat4(1.0f), _position) *
        //              glm::mat4_cast(rotQuat);

        // 看向 -z 方向，右手坐标系， 屏幕内测
        glm::vec3 forward = rotQuat * FMath::Vector::WorldForward;
        glm::vec3 target  = pos + forward;
        glm::vec3 up      = rotQuat * FMath::Vector::WorldUp;

        return FMath::lookAt(pos, target, up);
    }
};

} // namespace ya