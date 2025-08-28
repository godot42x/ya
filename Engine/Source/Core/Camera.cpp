#include "Camera.h"
namespace ya
{

void FreeCamera::update(const InputManager &inputManager, float deltaTime)
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
bool FreeCamera::handleKeyboardInput(const InputManager &inputManager, float deltaTime)
{
    bool  moved      = false;
    float moveAmount = _moveSpeed * deltaTime;

    // Calculate forward and right vectors from the camera's orientation
    glm::quat orientation = glm::quat(glm::radians(_rotation));
    glm::vec3 forward     = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right       = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up          = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Handle WASD movement
    if (inputManager.isKeyPressed(_forwardKey)) {
        _position += forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_backKey)) {
        _position -= forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_rightKey)) {
        _position += right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_leftKey)) {
        _position -= right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_upKey)) {
        _position += up * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_downKey)) {
        _position -= up * moveAmount;
        moved = true;
    }

    return moved;
}
bool FreeCamera::handleMouseRotation(const InputManager &inputManager, float deltaTime)
{
    if (inputManager.isMouseButtonPressed(rotateButton)) {
        glm::vec2 mouseDelta = inputManager.getMouseDelta();

        if (glm::length(mouseDelta) > 0.0f) {
            // Apply rotation (yaw around Y axis, pitch around X axis)
            _rotation.y -= mouseDelta.x * _rotationSpeed; //* deltaTime; this cannot multiply deltaTime, because mouseDelta is already frame-rate independent
            _rotation.x -= mouseDelta.y * _rotationSpeed; //* deltaTime;

            // Clamp pitch to avoid gimbal lock
            _rotation.x = glm::clamp(_rotation.x, -89.0f, 89.0f);

            // Normalize yaw angle
            while (_rotation.y > 180.0f)
                _rotation.y -= 360.0f;
            while (_rotation.y < -180.0f)
                _rotation.y += 360.0f;

            return true;
        }
    }

    return false;
}

} // namespace ya