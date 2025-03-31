#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


#include "Log.h"


struct Camera
{
    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
    glm::mat4 viewProjectionMatrix;

    glm::mat4 getViewProjectionMatrix() const { return viewProjectionMatrix; }

  protected:
    Camera() = default;
};

class EditorCamera : public Camera
{

    float fov         = 45.0f;
    float aspectRatio = 1.6 / 0.9f;
    float nearClip    = 0.1f;
    float farClip     = 1000.0f;

    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // pitch, yaw, roll

    enum EProjectionType
    {
        Perspective,
        Orthographic
    } projectionType;


  public:
    EditorCamera()
    {
    }
    ~EditorCamera();

    void setProjection(float fov, float aspectRatio, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Perspective;
        this->fov            = fov;
        if (fov < 1.f) {
            NE_WARN("FOV is too small {}", fov);
        }
        this->aspectRatio = aspectRatio;
        this->nearClip    = nearClip;
        this->farClip     = farClip;
        projectionMatrix  = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }

    void recalculateViewProjectionMatrix()
    {
        viewMatrix = glm::translate(glm::mat4(1.0f), -position) *
                     glm::mat4_cast(glm::quat(glm::vec3(rotation.x, rotation.y, rotation.z)));

        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }

    void setPosition(const glm::vec3 &position)
    {
        this->position = position;
        recalculateViewProjectionMatrix();
    }
    void setRotation(const glm::vec3 &rotation)
    {
        this->rotation = rotation;
        recalculateViewProjectionMatrix();
    }
};