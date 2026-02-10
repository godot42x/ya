#pragma once


#include "Core/Base.h"
#include "ECS/Component.h"
#include "Render/Core/Texture.h"

namespace ya
{

struct SkyboxComponent : public IComponent
{
    stdptr<Texture> cubemapTexture = nullptr;

    bool bDirty = true;
};

} // namespace ya