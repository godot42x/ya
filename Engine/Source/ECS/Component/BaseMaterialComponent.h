
#pragma once


#include "ECS/Component.h"

namespace ya
{

struct BaseMaterialComponent : public IComponent
{
    int colorType = 0; // 0: solid color, 1: texture
};

} // namespace ya