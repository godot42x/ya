
#pragma once


#include "ECS/Component.h"
#include "MaterialComponent.h"
#include "Render/Material/SimpleMaterial.h"

namespace ya
{

struct SimpleMaterialComponent : public MaterialComponent<SimpleMaterial>
{
};

} // namespace ya