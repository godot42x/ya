#pragma once
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"


namespace ya
{
// Light component for lighting
struct PointLightComponent : public IComponent
{
    YA_REFLECT_BEGIN(PointLightComponent)
    YA_REFLECT_FIELD(_type)
    YA_REFLECT_FIELD(_constant)
    YA_REFLECT_FIELD(_linear)
    YA_REFLECT_FIELD(_quadratic)

    YA_REFLECT_FIELD(_ambient, .color())
    YA_REFLECT_FIELD(_diffuse, .color())
    YA_REFLECT_FIELD(_specular, .color())

    YA_REFLECT_FIELD(_innerConeAngle, .manipulate(0.0f, 90.0f, 0.1f, ya::reflection::ManipulatorType::Slider))
    YA_REFLECT_FIELD(_outerConeAngle, .manipulate(0.0f, 90.0f, 0.1f, ya::reflection::ManipulatorType::Slider))
    YA_REFLECT_END()
  private:
  public:
    // TODO: one represent multiple light types?
    enum Type
    {
        Point = 0,
        Spot  = 1, // 手电筒
    };
    // TODO: reflect draw the enum in imgui
    int _type = Type::Point;

    // attenuation factors
    float _constant  = 1.0f;
    float _linear    = 0.09f;
    float _quadratic = 0.032f;

    glm::vec3 _ambient  = {1.0f, 1.0f, 1.0f};
    glm::vec3 _diffuse  = {1.0f, 1.0f, 1.0f};
    glm::vec3 _specular = {1.0f, 1.0f, 1.0f};

    float _innerConeAngle = 30.0f; // For spot lights
    float _outerConeAngle = 45.0f; // For spot lights

    PointLightComponent()                            = default;
    PointLightComponent(const PointLightComponent &) = default;
};
} // namespace ya