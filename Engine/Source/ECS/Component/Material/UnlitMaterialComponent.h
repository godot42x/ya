
/**
Unlit(no lighting )
*/
#pragma once


#include "MaterialComponent.h"
#include "Render/Core/Material.h"
namespace ya
{

struct UnlitMaterial : public Material
{
};


struct UnlitMaterialComponent : public MaterialComponent<UnlitMaterial>
{
};

} // namespace ya