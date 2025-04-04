#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Input/InputManager.h"
#include "Log.h"


struct Camera
{
    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
    glm::mat4 viewProjectionMatrix;

    const glm::mat4 &getViewProjectionMatrix() const { return viewProjectionMatrix; }

  protected:
    Camera()  = default;
    ~Camera() = default;
};

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
    } projectionType;

  public:
    EditorCamera() {}
    ~EditorCamera() {}

    void setPerspective(float fov, float aspectRatio, float nearClip, float farClip)
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

    void setOrthographic(float left, float right, float bottom, float top, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Orthographic;
        this->nearClip       = nearClip;
        this->farClip        = farClip;
        projectionMatrix     = glm::ortho(left, right, bottom, top, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }

    void update(const InputManager &inputManager, float deltaTime)
    {
        bool cameraChanged = false;

        // Handle keyboard movement (WASD + QE)
        if (handleKeyboardInput(inputManager, deltaTime)) {
            cameraChanged = true;
        }

        // Handle mouse rotation when right mouse button is held
        if (handleMouseRotation(inputManager, deltaTime)) {
            cameraChanged = true;
        }

        if (cameraChanged) {
            recalculateAll();
        }
    }

    bool handleKeyboardInput(const InputManager &inputManager, float deltaTime)
    {
        bool  moved      = false;
        float moveAmount = moveSpeed * deltaTime;

        // Calculate forward and right vectors from the camera's orientation
        glm::quat orientation = glm::quat(glm::radians(rotation));
        glm::vec3 forward     = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 right       = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up          = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

        // Handle WASD movement
        if (inputManager.isKeyPressed(forwardKey)) {
            position += forward * moveAmount;
            moved = true;
        }
        if (inputManager.isKeyPressed(backKey)) {
            position -= forward * moveAmount;
            moved = true;
        }
        if (inputManager.isKeyPressed(rightKey)) {
            position += right * moveAmount;
            moved = true;
        }
        if (inputManager.isKeyPressed(leftKey)) {
            position -= right * moveAmount;
            moved = true;
        }
        if (inputManager.isKeyPressed(upKey)) {
            position += up * moveAmount;
            moved = true;
        }
        if (inputManager.isKeyPressed(downKey)) {
            position -= up * moveAmount;
            moved = true;
        }

        return moved;
    }

    bool handleMouseRotation(const InputManager &inputManager, float deltaTime)
    {
        if (inputManager.isMouseButtonPressed(rotateButton)) {
            glm::vec2 mouseDelta = inputManager.getMouseDelta();

            if (glm::length(mouseDelta) > 0.0f) {
                // Apply rotation (yaw around Y axis, pitch around X axis)
                rotation.y -= mouseDelta.x * rotationSpeed;
                rotation.x -= mouseDelta.y * rotationSpeed;

                // Clamp pitch to avoid gimbal lock
                rotation.x = glm::clamp(rotation.x, -89.0f, 89.0f);

                // Normalize yaw angle
                while (rotation.y > 180.0f)
                    rotation.y -= 360.0f;
                while (rotation.y < -180.0f)
                    rotation.y += 360.0f;

                return true;
            }
        }

        return false;
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
};