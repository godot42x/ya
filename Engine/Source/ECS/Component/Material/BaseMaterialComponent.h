
#pragma once


#include "ECS/Component.h"
#include "MaterialComponent.h"
#include "Render/Core/Material.h"

namespace ya
{


struct BaseMaterial : public Material
{
    enum EColor
    {
        Normal   = 0,
        Texcoord = 1,
    };
    EColor colorType = Normal;
};

struct BaseMaterialComponent : public MaterialComponent<BaseMaterial>
{
};

} // namespace ya