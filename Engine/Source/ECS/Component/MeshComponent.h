#pragma once

#include "ECS/Component.h"
#include "Render/Mesh.h"



namespace ya
{


struct MeshComponent : public IComponent
{
    Mesh *mesh;
};
} // namespace ya