#pragma once

#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

namespace ya
{

struct DirectionalLightComponent : public IComponent
{
    YA_REFLECT_BEGIN(DirectionalLightComponent)
    YA_REFLECT_FIELD(_direction)
    YA_REFLECT_FIELD(_ambient, .color())
    YA_REFLECT_FIELD(_diffuse, .color())
    YA_REFLECT_FIELD(_specular, .color())
    YA_REFLECT_END()

    // optional: use the TransformComponent's rotation to determine direction first
    glm::vec3 _direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)); 

    glm::vec3 _ambient   = glm::vec3(10.0f / 256.0f);
    glm::vec3 _diffuse   = glm::vec3(30.0f / 256.0f);
    glm::vec3 _specular  = glm::vec3(31.0f / 256.0f);

    DirectionalLightComponent()                                  = default;
    DirectionalLightComponent(const DirectionalLightComponent &) = default;
};

} // namespace ya
