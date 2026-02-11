
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
        static constexpr bool      ColumnMajor  = true; // GLM 默认是列主序

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

        return FMath::lookAt(pos,
                             target,
                             up);
    }


    static glm::mat3 build_scale_mat3(glm::vec2 scale)
    {
        return glm::mat3{
            {scale.x, 0.0f, 0.0f}, // 列0
            {0.0f, scale.y, 0.0f}, // 列1
            {0.0f, 0.0f, 1.0f}     // 列2（齐次维度）
        };
    }

    // 用GLM构造2D UV旋转矩阵（mat3，绕z轴）
    static glm::mat3 build_rotate_mat3(float deg)
    {
        float rad = glm::radians(deg);
        float c   = glm::cos(rad);
        float s   = glm::sin(rad);
        return glm::mat3{
            {c, -s, 0.0f},     // 列0
            {s, c, 0.0f},      // 列1
            {0.0f, 0.0f, 1.0f} // 列2
        };
    }

    static glm::mat3 build_translate_mat3(glm::vec2 translation)
    {
        return {
            {1.0f, 0.0f, translation.x}, // 列0
            {0.0f, 1.0f, translation.y}, // 列1
            {0.0f, 0.0f, 1.0f}           // 列2（齐次坐标）
        };
    }

    static glm::mat3 build_transform_mat3(const glm::vec2 &translation, float rotationDeg, const glm::vec2 &scale)
    {
        float rad = glm::radians(rotationDeg);
        float c   = glm::cos(rad);
        float s   = glm::sin(rad);
        return glm::mat3{
            {scale.x * c, scale.x * -s, 0.0f},   // 列0
            {scale.y * s, scale.y * c, 0.0f},    // 列1
            {translation.x, translation.y, 1.0f} // 列2
        };
    }

    static glm::mat4 build_transform_mat4(glm::vec3 translation, glm::vec3 rotationDeg, glm::vec3 scale)
    {
        // glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);
        // glm::mat4 rotMat   = glm::yawPitchRoll(glm::radians(rotationDeg.y), glm::radians(rotationDeg.x), glm::radians(rotationDeg.z));
        // glm::mat4 transMat = glm::translate(glm::mat4(1.0f), translation
        return glm::mat4(1.0f); // 临时返回单位矩阵，避免编译错误
    }

    static glm::mat4 dropTranslation(const glm::mat4 &mat)
    {
        return {
            {mat[0][0], mat[0][1], mat[0][2], 0.0f},
            {mat[1][0], mat[1][1], mat[1][2], 0.0f},
            {mat[2][0], mat[2][1], mat[2][2], 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
    }
};

} // namespace ya