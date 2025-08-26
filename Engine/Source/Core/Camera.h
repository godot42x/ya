#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <SDL3/SDL_events.h>

#include "Input/InputManager.h"
#include "KeyCode.h"
#include "Log.h"

namespace ya
{

struct Camera
{
    glm::mat4 projectionMatrix     = {};
    glm::mat4 viewMatrix           = {};
    glm::mat4 viewProjectionMatrix = {};

    [[nodiscard]] const glm::mat4 &getViewProjectionMatrix() const { return viewProjectionMatrix; }

    glm::mat4 getViewMatrix() const { return viewMatrix; }
    glm::mat4 getProjectionMatrix() const { return projectionMatrix; }

    virtual ~Camera() = default;
};



struct FreeCamera : public Camera
{
    float fov         = 45.0f;
    float aspectRatio = 1.6 / 0.9f;
    float nearClip    = 0.1f;
    float farClip     = 1000.0f;

    glm::vec3 position = {0.0f, 0.0f, 0.0f}; // Start a bit back from the origin
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // pitch, yaw, roll

    // Camera control settings
    float moveSpeed     = 5.0f; // Units per second
    float rotationSpeed = 45.f; // Degrees per second

    // Movement keys (configurable)
    SDL_Keycode forwardKey = SDLK_W;
    SDL_Keycode backKey    = SDLK_S;
    SDL_Keycode leftKey    = SDLK_A;
    SDL_Keycode rightKey   = SDLK_D;
    SDL_Keycode upKey      = SDLK_Q;
    SDL_Keycode downKey    = SDLK_E;

    // Mouse button for rotation
    Uint8 rotateButton = SDL_BUTTON_RIGHT;

    enum EProjectionType
    {
        Perspective,
        Orthographic
    } projectionType = Perspective;

  public:
    FreeCamera() : Camera() {}
    ~FreeCamera() {}

    void setPerspective(float fov, float aspectRatio, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Perspective;
        this->fov            = fov;
        if (fov < 1.f) {
            YA_CORE_WARN("FOV is too small {}", fov);
        }
        this->aspectRatio = aspectRatio;
        this->nearClip    = nearClip;
        this->farClip     = farClip;
        projectionMatrix  = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }

    void setOrthographic(float left, float right, float bottom, float top, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Orthographic;
        this->nearClip       = nearClip;
        this->farClip        = farClip;
        projectionMatrix     = glm::ortho(left, right, bottom, top, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }



    void recalculateViewMatrix()
    {
        const glm::quat rotQuat = glm::quat(glm::radians(rotation));

        viewMatrix = glm::translate(glm::mat4(1.0f), position) *
                     glm::mat4_cast(rotQuat);

        viewMatrix = glm::inverse(viewMatrix);
    }

    void recalculateViewProjectionMatrix()
    {
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }

    void recalculateAll()
    {
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    void setPosition(const glm::vec3 &position)
    {
        this->position = position;
        recalculateAll();
    }

    void setRotation(const glm::vec3 &rotation)
    {
        this->rotation = rotation;
        recalculateAll();
    }

    void setPositionAndRotation(const glm::vec3 &position, const glm::vec3 &rotation)
    {
        this->position = position;
        this->rotation = rotation;
        recalculateAll();
    }

    void setAspectRatio(float aspectRatio)
    {
        this->aspectRatio = aspectRatio;
        if (projectionType == EProjectionType::Perspective) {
            projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
        }
        else {
            projectionMatrix = glm::ortho(-aspectRatio, aspectRatio, -1.0f, 1.0f, nearClip, farClip);
        }
        recalculateViewProjectionMatrix();
    }


  public: // Camera control methods
    void update(const InputManager &inputManager, float deltaTime);

  private:
    bool handleKeyboardInput(const InputManager &inputManager, float deltaTime);
    bool handleMouseRotation(const InputManager &inputManager, float deltaTime);
};


struct OrbitCamera : public Camera
{
    glm::vec3 focalPoint = {0.0f, 0.0f, 0.0f};
    float     distance   = 10.0f;

    glm::vec3 position;
    glm::vec3 rotation; // pitch, yaw, roll

    EMouse::T rotateButton = EMouse::Left;
    EMouse::T zoomButton   = EMouse::Middle;

  public:
    void setDistance(float distance)
    {
        this->distance = distance;
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    void setFocalPoint(const glm::vec3 &focalPoint)
    {
        this->focalPoint = focalPoint;
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    void recalculateViewMatrix()
    {
    }

    void recalculateViewProjectionMatrix()
    {
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }

    void update(const InputManager &inputManager, float deltaTime)
    {
        if (inputManager.isMouseButtonPressed(ya::EMouse::Left))
        {
            handleMouseRotation(inputManager, deltaTime);
        }
    }

  private:
    bool handleMouseRotation(const InputManager &inputManager, float deltaTime);
    bool handleMouseZoom(const InputManager &inputManager, float deltaTime);
};
} // namespace ya