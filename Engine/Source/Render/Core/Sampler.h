
#pragma once
#include "Handle.h"
#include "PlatBase.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct SamplerHandleTag
{};
using SamplerHandle = Handle<SamplerHandleTag>;



struct Sampler : public plat_base<Sampler>
{
    SamplerDesc _desc;

    static stdptr<Sampler> create(const SamplerDesc &desc);

    /**
     * @brief Get the platform-specific handle for this sampler
     * @return void* Platform handle (e.g., VkSampler for Vulkan)
     */
    virtual SamplerHandle getHandle() const = 0;
};
} // namespace ya