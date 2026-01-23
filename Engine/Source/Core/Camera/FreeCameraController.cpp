#include "FreeCameraController.h"


namespace ya
{
void FreeCameraController::update(FreeCamera &camera, const InputManager &inputManager, float deltaTime)
{
    bool cameraChanged = false;

    if (handleKeyboardInput(camera._position, camera._rotation, inputManager, deltaTime)) {
        cameraChanged = true;
    }

    if (handleMouseRotation(camera._rotation, inputManager, deltaTime)) {
        cameraChanged = true;
    }

    if (cameraChanged) {
        camera.recalculateAll();
    }
}

void FreeCameraController::update(TransformComponent &tc, CameraComponent &cc, const InputManager &inputManager, const Extent2D &extent, float dt)
{
    bool tcDirty = false;
    if (handleKeyboardInput(tc._position, tc._rotation, inputManager, dt)) {
        tcDirty = true;
    }
    if (handleMouseRotation(tc._rotation, inputManager, dt)) {
        tcDirty = true;
    }
    if (tcDirty) {
        tc._localDirty = true;
        tc._worldDirty = true;
    }

    if (extent.height > 0) {
        cc._aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    }
}

bool FreeCameraController::handleKeyboardInput(glm::vec3 &pos, const glm::vec3 &rot, const InputManager &inputManager, float deltaTime)
{

    bool  moved      = false;
    float moveAmount = _moveSpeed * deltaTime;

    glm::quat orientation = glm::quat(glm::radians(rot));
    glm::vec3 forward     = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right       = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up          = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    if (inputManager.isKeyPressed(_forwardKey)) {
        pos += forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_backKey)) {
        pos -= forward * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_rightKey)) {
        pos += right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_leftKey)) {
        pos -= right * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_upKey)) {
        pos += up * moveAmount;
        moved = true;
    }
    if (inputManager.isKeyPressed(_downKey)) {
        pos -= up * moveAmount;
        moved = true;
    }

    return moved;
}

bool FreeCameraController::handleMouseRotation(glm::vec3 &rot, const InputManager &inputManager, float deltaTime)
{
    if (!inputManager.isMouseButtonPressed(_rotateButton)) {
        return false;
    }

    glm::vec2 mouseDelta = inputManager.getMouseDelta();
    if (glm::length(mouseDelta) <= 0.0f) {
        return false;
    }

    rot.y -= mouseDelta.x * _rotationSpeed * deltaTime; // yaw: left/right
    rot.x -= mouseDelta.y * _rotationSpeed * deltaTime; // pitch: upward/downward

    rot.x = glm::clamp(rot.x, -89.0f, 89.0f);

    while (rot.y > 180.0f)
        rot.y -= 360.0f;
    while (rot.y < -180.0f)
        rot.y += 360.0f;
    rot.z = 0.0f; // No roll

    return true;
}

} // namespace ya
