#pragma once
#include "Core/Camera/CameraController.h"

#include "Core/Input/InputManager.h"
#include "Core/KeyCode.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"



namespace ya
{

// Controller for the ECS camera entity (TransformComponent + CameraComponent)
struct OrbitCameraController : public CameraController
{
    float     _mouseSensitivity = 15.0f;
    float     _zoomSensitivity  = 1000.f;
    EMouse::T _rotateButton     = EMouse::Right;

  public:

    void update(TransformComponent &tc, CameraComponent &cc, const InputManager &inputManager, const Extent2D &extent, float dt)
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
};

} // namespace ya