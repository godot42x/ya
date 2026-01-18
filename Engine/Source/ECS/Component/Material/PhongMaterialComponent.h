#pragma once

#include "ECS/Component/Material/MaterialComponent.h"
#include "Render/Material/PhongMaterial.h"

namespace ya
{

struct PhongMaterialComponent : public MaterialComponent<PhongMaterial>
{
};


} // namespace ya