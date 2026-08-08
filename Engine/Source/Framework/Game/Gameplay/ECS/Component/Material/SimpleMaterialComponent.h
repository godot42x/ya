
#pragma once


#include "Framework/Game/Gameplay/ECS/Component.h"
#include "MaterialComponent.h"
#include "Framework/Game/Render/Render3D/Material/SimpleMaterial.h"

namespace ya
{

struct SimpleMaterialComponent : public MaterialComponent<SimpleMaterial>
{
};

} // namespace ya