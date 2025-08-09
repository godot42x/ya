#pragma once

#include <glm/glm.hpp>

struct Camera
{
    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
    glm::mat4 viewProjectionMatrix;

    [[nodiscard]] const glm::mat4 &getViewProjectionMatrix() const { return viewProjectionMatrix; }

    glm::mat4 getViewMatrix() const { return viewMatrix; }
    glm::mat4 getProjectionMatrix() const { return projectionMatrix; }

  protected:
    Camera()  = default;
    ~Camera() = default;
};