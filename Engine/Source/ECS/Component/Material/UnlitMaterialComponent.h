
/**
Unlit(no lighting )
*/
#pragma once


#include "MaterialComponent.h"
#include "Render/Material/UnlitMaterial.h"
namespace ya
{



struct UnlitMaterialComponent : public MaterialComponent<UnlitMaterial>
{
};

} // namespace ya