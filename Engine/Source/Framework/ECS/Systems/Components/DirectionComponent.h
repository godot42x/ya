#pragma once

#include "ECS/Component.h"

namespace ya
{


// draw:
//  1. world forward
//  2. if have transfrom comp, also draw the transform component's forward
struct DirectionComponent : public IComponent
{
    YA_REFLECT_BEGIN(DirectionComponent)
    YA_REFLECT_FIELD(_indicatorModelPath)
    YA_REFLECT_END()

    FAssetPath _indicatorModelPath;
};
} // namespace ya