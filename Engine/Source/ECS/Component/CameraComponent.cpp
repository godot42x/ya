#include "CameraComponent.h"
#include "ECS/Component/TransformComponent.h"


namespace ya
{

// TODO: a camera should only define the effect:
//  1. projection or orthographic
//  2. other fov some camera effect
// So we should not take the view form there, Should there
// come a CameraController to do this work...
glm::mat4 CameraComponent::getOrbitView() const
{
    if (getOwner() && getOwner()->hasComponent<TransformComponent>()) {
        auto tc = getOwner()->getComponent<TransformComponent>();

        float pitch = glm::radians(tc->_rotation.x);
        float yaw   = glm::radians(tc->_rotation.y);

        glm::vec3 dir;

        // euler angle 计算顺序: yaw (dir.x) -> pitch (dir.y) -> roll (dir.z)

        // 我们希望 roll 不影响视角的产生奇怪的旋转, 比如把头横过来或者倒过来看东西
        // 且按照欧拉角的计算顺序, roll 是最后一个旋转, 所以我们忽略 roll 的影响
        // 所以 dir.y 只和 pitch 有关
        // pitch 就是绕 x 轴旋转的角度
        // sin(pitch): 角度在 Y 轴的投影长度
        dir.y = std::sin(pitch);

        // 然后我们看 xoz 平面, yaw 就是绕 y 轴旋转的角度
        // 当 pitch = 0 时, (dir.x, dir.z) 就是 xoz 上的一个坐标/vec2
        dir.x = std::sin(yaw);
        dir.z = std::cos(yaw);
        // 受到了 pitch 的影响(绕 x 轴旋转), 需要乘以 cost(pitch)
        // 想象这个平面向量进行了抬升/降低的操作
        dir.x *= std::cos(pitch);
        dir.z *= std::cos(pitch);

        dir = glm::normalize(glm::radians(dir));
        dir = -dir; // 取逆，因为是相机到目标点的方向

        tc->_position = _focusPoint + dir * _distance;


        return FMath::lookAt(
            tc->_position,
            _focusPoint,
            FMath::Vector::WorldUp);
    }
    return FMath::lookAt(
        glm::vec3(0, 0, _distance) + _focusPoint,
        _focusPoint,
        FMath::Vector::WorldUp);
}

glm::mat4 CameraComponent::getFreeView() const
{
    if (getOwner() && getOwner()->hasComponent<TransformComponent>()) {
        auto tc = getOwner()->getComponent<TransformComponent>();

        float pitch = glm::radians(tc->_rotation.x);
        float yaw   = glm::radians(tc->_rotation.y);
        float roll  = glm::radians(tc->_rotation.z);

        glm::quat rotQuat = glm::quat(glm::vec3(pitch, yaw, roll));

        glm::vec3 forward = rotQuat * FMath::Vector::WorldForward;
        glm::vec3 target  = tc->_position + forward;

        glm::vec3 cameraUp = rotQuat * FMath::Vector::WorldUp;

        return FMath::lookAt(tc->_position, target, cameraUp);
    }

    return FMath::lookAt(
        glm::vec3(0, 0, _distance) + _focusPoint,
        _focusPoint,
        FMath::Vector::WorldUp);
}

} // namespace ya
