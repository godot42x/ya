#pragma once
#include "Core/Camera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <SDL3/SDL_events.h>

#include "Input/InputManager.h"
#include "Log.h"



struct EditorCamera : public Camera
{
    float fov         = 45.0f;
    float aspectRatio = 1.6 / 0.9f;
    float nearClip    = 0.1f;
    float farClip     = 1000.0f;

    glm::vec3 position = {0.0f, 0.0f, 0.0f}; // Start a bit back from the origin
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // pitch, yaw, roll

    // Camera control settings
    float moveSpeed     = 5.0f; // Units per second
    float rotationSpeed = 0.2f; // Degrees per pixel

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
    EditorCamera() : Camera() {}
    ~EditorCamera() {}

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