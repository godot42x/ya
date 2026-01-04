#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"

#include "Core/Math/Math.h"

#include "Core/Reflection/UnifiedReflection.h"

namespace ya
{


struct CameraComponent : public IComponent
{
    YA_REFLECT(CameraComponent,
               PROP(_primary),
               PROP(_fixedAspectRatio),
               PROP(_fov),
               PROP(_aspectRatio),
               PROP(_nearClip),
               PROP(_farClip),
               PROP(_distance),
               PROP(_focusPoint))

    bool _primary          = true; // TODO: think about moving to Scene
    bool _fixedAspectRatio = false;

    float _fov         = 45.0f;
    float _aspectRatio = 16.0f / 9.0f;
    float _nearClip    = 0.1f;
    float _farClip     = 1000.0f;

    float     _distance   = 6.f;
    glm::vec3 _focusPoint = glm::vec3(0.f, 0.f, 0.f);

    // TODO: cache
    glm::mat4 getProjection() const
    {
        return FMath::perspective(glm::radians(_fov), _aspectRatio, _nearClip, _farClip);
    }

    // TODO: a camera should only define the effect:
    //  1. projection or orthographic
    //  2. other fov some camera effect
    // So we should not take the view form there, Should there
    // come a CameraController to do this work...
    glm::mat4 getView() const
    {
        if (getOwner() && getOwner()->hasComponent<TransformComponent>()) {
            auto tc = getOwner()->getComponent<TransformComponent>();

            float yaw   = tc->_rotation.x;
            float pitch = tc->_rotation.y;

            glm::vec3 dir;
            dir.x = std::cos(glm::radians(pitch)) * sin(glm::radians(yaw));
            dir.y = std::sin(glm::radians(pitch));
            dir.z = std::cos(glm::radians(pitch)) * cos(glm::radians(yaw));

            tc->_position = _focusPoint + dir * _distance;


            return FMath::lookAt(
                tc->_position,
                _focusPoint,
                glm::vec3(0, 1, 0));
        }
        return FMath::lookAt(
            glm::vec3(0, 0, _distance) + _focusPoint,
            _focusPoint,
            glm::vec3(0, 1, 0));
    }
    glm::mat4 getViewProjection() const { return getProjection() * getView(); }

  public:

    void setAspectRatio(float aspectRatio) { _aspectRatio = aspectRatio; }
};



} // namespace ya