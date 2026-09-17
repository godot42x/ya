#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include "Scene3D/TransformComponent.h"

#include <limits>


namespace ya
{

namespace
{

/// Where the camera actually is in the world.
///
/// A camera renders from its position in the scene, so a view built from the
/// authored local transform alone is only correct while the camera happens to
/// be a root node. The mesh drawn for the camera, the FOV wireframe and the
/// picking all resolve through the world matrix, so the view has to resolve it
/// the same way or the camera is drawn in one place and renders from another.
struct FOwnerWorldPose
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

bool resolveOwnerWorldPose(Entity* owner, FOwnerWorldPose& out)
{
    if (!owner || !owner->hasComponent<TransformComponent>()) {
        return false;
    }

    auto* tc = owner->getComponent<TransformComponent>();
    if (!tc) {
        return false;
    }

    // No-op when the transform is already clean, which is the normal case: the
    // TransformSystem runs before anything reads a view this tick.
    TransformSystem::computeWorldMatrix(tc);

    const glm::mat4& world = tc->getWorldMatrix();
    out.position           = glm::vec3(world[3]);

    // Scale is stripped before the rotation is read back: a scaled ancestor
    // belongs to the mesh, not to the camera basis.
    const glm::vec3 basisX = glm::vec3(world[0]);
    const glm::vec3 basisY = glm::vec3(world[1]);
    const glm::vec3 basisZ = glm::vec3(world[2]);
    if (glm::length2(basisX) < std::numeric_limits<float>::epsilon() ||
        glm::length2(basisY) < std::numeric_limits<float>::epsilon() ||
        glm::length2(basisZ) < std::numeric_limits<float>::epsilon()) {
        return false;
    }

    out.rotation = glm::normalize(
        glm::quat_cast(glm::mat3(glm::normalize(basisX), glm::normalize(basisY), glm::normalize(basisZ))));
    return true;
}

} // namespace

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
    if (FOwnerWorldPose pose; resolveOwnerWorldPose(getOwner(), pose)) {
        const glm::vec3 forward = pose.rotation * FMath::Vector::WorldForward;
        const glm::vec3 up      = pose.rotation * FMath::Vector::WorldUp;

        return FMath::lookAt(pose.position, pose.position + forward, up);
    }

    return FMath::lookAt(
        glm::vec3(0, 0, _distance) + _focusPoint,
        _focusPoint,
        FMath::Vector::WorldUp);
}

} // namespace ya
