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

    if (handleMouseRotation(camera, inputManager, deltaTime)) {
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

bool FreeCameraController::handleMouseRotation(FreeCamera &camera, const InputManager &inputManager, float deltaTime)
{
    if (!inputManager.isMouseButtonPressed(_rotateButton)) {
        return false;
    }

    glm::vec2 mouseDelta = inputManager.getMouseDelta();
    if (glm::length(mouseDelta) <= 0.0f) {
        return false;
    }

    camera._rotation.y -= mouseDelta.x * _rotationSpeed * deltaTime; // yaw: left/right
    camera._rotation.x -= mouseDelta.y * _rotationSpeed * deltaTime; // pitch: upward/downward

    camera._rotation.x = glm::clamp(camera._rotation.x, -89.0f, 89.0f);

    while (camera._rotation.y > 180.0f)
        camera._rotation.y -= 360.0f;
    while (camera._rotation.y < -180.0f)
        camera._rotation.y += 360.0f;
    camera._rotation.z = 0.0f; // No roll

    return true;
}


void OrbitCameraController::update(TransformComponent &tc, CameraComponent &cc, const InputManager &inputManager, const Extent2D &extent, float dt)
{
    if (extent.height > 0) {
        cc.setAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    }

    if (inputManager.isMouseButtonPressed(_rotateButton)) {
        glm::vec2 mouseDelta = inputManager.getMouseDelta();
        if (glm::length(mouseDelta) > 0.0f) {
            float pitch = tc._rotation.x;
            float yaw   = tc._rotation.y;

            if constexpr (FMath::Vector::IsRightHanded) {
                // 平面坐标系列,
                // theta ++ 为正（逆时针方向,由第一象限向第四象限转动)
                // theta -- 为负(顺时针方向，由第四象限向第一象限转动)

                //  x >0 向右拖动,  希望物品在xoz平面上， 绕y轴 逆时针运动
                // 相机则是顺时针运动，所以 yaw 增加
                yaw += mouseDelta.x * _mouseSensitivity * dt;
                // y >0 向上拖动， 希望物品在 yoz 平面上， 绕 x 轴逆时针运动
                // 相机则是顺时针运动，所以 pitch 减少
                pitch -= mouseDelta.y * _mouseSensitivity * dt;
            }

            if (pitch > 89.f) {
                pitch = 89.f;
            }
            else if (pitch < -89.f) {
                pitch = -89.f;
            }

            tc._rotation.x = pitch;
            tc._rotation.y = yaw;
        }
    }

    glm::vec2 scrollDelta = inputManager.getMouseScrollDelta();
    cc._distance -= scrollDelta.y * _zoomSensitivity * dt;
    cc._distance = glm::max(cc._distance, 0.1f);
}

} // namespace ya
