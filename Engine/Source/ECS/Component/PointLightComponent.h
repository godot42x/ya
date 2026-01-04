#pragma once
#include "ECS/Component.h"
#include "Core/Reflection/UnifiedReflection.h"

namespace ya
{
// Light component for lighting
struct PointLightComponent : public IComponent
{
    YA_REFLECT(PointLightComponent, 
        PROP(_color), 
        PROP(_intensity), 
        PROP(_range)
    )
  private:
  public:
    // TODO: one represent multiple light types?
    // enum class Type
    // {
    //     Directional,
    //     Point,
    //     Spot // 手电筒
    // };
    // Type      _type           = Type::Directional;

    glm::vec3 _color     = {1.0f, 1.0f, 1.0f};
    float     _intensity = 1.0f;
    float     _range     = 50.0f; // For point and spot lights

    // float _innerConeAngle = 30.0f; // For spot lights
    // float _outerConeAngle = 45.0f; // For spot lights

    PointLightComponent()                            = default;
    PointLightComponent(const PointLightComponent &) = default;
};
} // namespace ya