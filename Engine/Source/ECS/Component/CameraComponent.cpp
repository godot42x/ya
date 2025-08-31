#include "CameraComponent.h"

#include "ECS/Entity.h"
#include "TransformComponent.h"

namespace ya
{

// TODO: a camera should only define the effect:
//  1. projection or orthographic
//  2. other fov some camera effect
// So we should not take the view form there, Should there
// come a CameraController to do this work...

glm::mat4 CameraComponent::getView() const
{
    if (getOwner() && getOwner()->hasComponent<TransformComponent>()) {
        auto &tc = getOwner()->getComponent<TransformComponent>();

        float yaw   = tc._rotation.x;
        float pitch = tc._rotation.y;

        glm::vec3 dir;
        dir.x = std::cos(glm::radians(pitch)) * sin(glm::radians(yaw));
        dir.y = std::sin(glm::radians(pitch));
        dir.z = std::cos(glm::radians(pitch)) * cos(glm::radians(yaw));

        tc._position = _focusPoint + dir * _distance;

        return glm::lookAt(
            tc._position,
            _focusPoint,
            glm::vec3(0, 1, 0));
    }
    return glm::lookAt(
        glm::vec3(0, 0, _distance) + _focusPoint,
        _focusPoint,
        glm::vec3(0, 1, 0));
}

} // namespace ya
