#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"

#include "Core/Math/Math.h"

namespace ya
{


struct CameraComponent : public IComponent, public MetaRegister
{

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
        return glm::perspectiveRH_ZO(glm::radians(_fov), _aspectRatio, _nearClip, _farClip);
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

    void registerAll() override
    {
        CameraComponent::registerReflection();
    }

    // ========================================================================
    // 反射注册 - 自动生成序列化
    // ========================================================================
    static void registerReflection()
    {
        Register<CameraComponent>("CameraComponent")
            .constructor<>()
            .property("primary", &CameraComponent::_primary)
            .property("fixedAspectRatio", &CameraComponent::_fixedAspectRatio)
            .property("fov", &CameraComponent::_fov)
            .property("aspectRatio", &CameraComponent::_aspectRatio)
            .property("nearClip", &CameraComponent::_nearClip)
            .property("farClip", &CameraComponent::_farClip)
            .property("distance", &CameraComponent::_distance)
            .property("focusPoint", &CameraComponent::_focusPoint);
    }
};



}; // namespace ya