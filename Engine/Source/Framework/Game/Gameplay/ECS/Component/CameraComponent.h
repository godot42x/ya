#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"

#include "Core/Math/Math.h"

#include "Core/Reflection/Reflection.h"

namespace ya
{


struct CameraComponent : public IComponent
{
    YA_REFLECT_BEGIN(CameraComponent)
    YA_REFLECT_FIELD(bPrimary)
    YA_REFLECT_FIELD(_fixedAspectRatio)
    YA_REFLECT_FIELD(_fov)
    YA_REFLECT_FIELD(_aspectRatio)
    YA_REFLECT_FIELD(_nearClip)
    YA_REFLECT_FIELD(_farClip)
    YA_REFLECT_FIELD(_distance)
    YA_REFLECT_FIELD(_focusPoint)
    YA_REFLECT_END()

    bool bPrimary          = false; // TODO: think about moving to Scene
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

    glm::mat4 getOrbitView() const;
    glm::mat4 getFreeView() const;
    glm::mat4 getViewProjection() const { return getProjection() * getFreeView(); }
    glm::mat4 getOrbitViewProjection() const { return getProjection() * getOrbitView(); }

  public:

    void setAspectRatio(float aspectRatio) { _aspectRatio = aspectRatio; }
};



} // namespace ya