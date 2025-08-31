#include "CameraComponent.h"

#include "ECS/Entity.h"
#include "TransformComponent.h"

namespace ya
{

glm::mat4 CameraComponent::getView() const
{
    if (getOwner() && getOwner()->hasComponent<TransformComponent>()) {
        auto &tc = getOwner()->getComponent<TransformComponent>();
        return glm::lookAt(
            glm::vec3(0, 0, _distance) + tc._position,
            tc._position,
            glm::vec3(0, 1, 0));
    }
    return glm::lookAt(
        glm::vec3(0, 0, _distance) + _focusPoint,
        _focusPoint,
        glm::vec3(0, 1, 0));
}

} // namespace ya
