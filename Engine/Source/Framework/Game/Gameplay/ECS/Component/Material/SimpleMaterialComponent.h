
#pragma once


#include "ECS/Component.h"
#include "MaterialComponent.h"

namespace ya
{

struct SimpleMaterial;

struct SimpleMaterialComponent : public MaterialComponent<SimpleMaterial>
{
};

} // namespace ya
