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
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_intensity)
    YA_REFLECT_FIELD(_range)
    YA_REFLECT_FIELD(_innerConeAngle)
    YA_REFLECT_FIELD(_outerConeAngle)
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

    glm::vec3 _color     = {1.0f, 1.0f, 1.0f};
    float     _intensity = 1.0f;
    float     _range     = 50.0f; // For point and spot lights

    float _innerConeAngle = 30.0f; // For spot lights
    float _outerConeAngle = 45.0f; // For spot lights

    PointLightComponent()                            = default;
    PointLightComponent(const PointLightComponent &) = default;
};
} // namespace ya