#include "EditorCamera.h"

void EditorCamera::update(const InputManager &inputManager, float deltaTime)
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
bool EditorCamera::handleKeyboardInput(const InputManager &inputManager, float deltaTime)
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
bool EditorCamera::handleMouseRotation(const InputManager &inputManager, float deltaTime)
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
