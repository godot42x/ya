
#pragma once


#include "ECS/Component.h"
#include "ECS/Component/Material/MaterialComponent.h"

namespace ya
{

struct SimpleMaterial;

struct SimpleMaterialComponent : public MaterialComponent<SimpleMaterial>
{
    void onEdit() override { invalidate(); }
};

} // namespace ya
