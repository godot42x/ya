#pragma once


#include "Core/Base.h"
#include "ECS/Component.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Texture.h"

namespace ya
{

struct SkyboxComponent : public IComponent
{
    stdptr<Texture> cubemapTexture = nullptr;

    /// Shared cubemap descriptor set, allocated from App-layer pool.
    /// Written by ResourceResolveSystem, bound by SkyBoxSystem and PhongMaterialSystem.
    DescriptorSetHandle cubeMapDS = nullptr;

    bool bDirty = true;
};

} // namespace ya