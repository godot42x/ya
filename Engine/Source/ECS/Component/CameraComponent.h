#pragma once


#include "ECS/Component.h"
namespace ya
{


struct CameraComponent : public IComponent
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
        return glm::perspective(glm::radians(_fov), _aspectRatio, _nearClip, _farClip);
    }
    glm::mat4 getView() const;
    glm::mat4 getViewProjection() const { return getProjection() * getView(); }

  public:

    void setAspectRatio(float aspectRatio) { _aspectRatio = aspectRatio; }
};


}; // namespace ya