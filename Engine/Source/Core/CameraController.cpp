#include "Core/CameraController.h"
#include "Math/GLM.h"

namespace ya
{

void FreeCameraController::update(FreeCamera &camera, const InputManager &inputManager, float deltaTime)
{
    bool cameraChanged = false;

    if (handleKeyboardInput(camera, inputManager, deltaTime)) {
        cameraChanged = true;
    }

    if (handleMouseRotation(camera, inputManager)) {
        cameraChanged = true;
    }

    if (cameraChanged) {
        camera.recalculateAll();
    }
}

bool FreeCameraController::handleKeyboardInput(FreeCamera &camera, const InputManager &inputManager, float deltaTime)
{
    bool  moved      = false;
    float moveAmount = _moveSpeed * deltaTime;

    glm::quat orientation = glm::quat(glm::radians(camera._rotation));
    glm::vec3 forward     = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right       = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up          = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    if (inputManager.isKeyPressed(_forwardKey)) {
        camera._position += forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_backKey)) {
        camera._position -= forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_rightKey)) {
        camera._position += right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_leftKey)) {
        camera._position -= right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_upKey)) {
        camera._position += up * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_downKey)) {
        camera._position -= up * moveAmount;
        moved = true;
    }

    return moved;
}

bool FreeCameraController::handleMouseRotation(FreeCamera &camera, const InputManager &inputManager)
{
    if (!inputManager.isMouseButtonPressed(_rotateButton)) {
        return false;
    }

    glm::vec2 mouseDelta = inputManager.getMouseDelta();
    if (glm::length(mouseDelta) <= 0.0f) {
        return false;
    }

    camera._rotation.y -= mouseDelta.x * _rotationSpeed;
    camera._rotation.x -= mouseDelta.y * _rotationSpeed;

    camera._rotation.x = glm::clamp(camera._rotation.x, -89.0f, 89.0f);

    while (camera._rotation.y > 180.0f)
        camera._rotation.y -= 360.0f;
    while (camera._rotation.y < -180.0f)
        camera._rotation.y += 360.0f;

    return true;
}


void OrbitCameraController::update(TransformComponent &tc, CameraComponent &cc, const InputManager &inputManager, const Extent2D &extent)
{
    if (extent.height > 0) {
        cc.setAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    }

    if (inputManager.isMouseButtonPressed(_rotateButton)) {
        glm::vec2 mouseDelta = inputManager.getMouseDelta();
        if (glm::length(mouseDelta) > 0.0f) {
            float yaw   = tc._rotation.x;
            float pitch = tc._rotation.y;

            yaw -= mouseDelta.x * _mouseSensitivity;
            pitch -= mouseDelta.y * _mouseSensitivity;

            if (pitch > 89.f) {
                pitch = 89.f;
            }
            else if (pitch < -89.f) {
                pitch = -89.f;
            }

            tc._rotation.x = yaw;
            tc._rotation.y = pitch;
        }
    }

    glm::vec2 scrollDelta = inputManager.getMouseScrollDelta();
    cc._distance -= scrollDelta.y * _zoomSensitivity;
}

} // namespace ya
