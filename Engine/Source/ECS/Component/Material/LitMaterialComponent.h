

/**
Lighting
*/
#pragma once


#include "MaterialComponent.h"
#include "Render/Material/LitMaterial.h"

namespace ya
{



struct LitMaterialComponent : public MaterialComponent<LitMaterial>
{
};

} // namespace ya