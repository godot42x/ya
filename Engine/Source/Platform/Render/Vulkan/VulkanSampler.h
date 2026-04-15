
#pragma once

#include "Core/Base.h"
#include "Render/Core/Sampler.h"
#include "Render/RenderDefines.h"
#include "vulkan/vulkan.h"


namespace ya
{

struct VulkanSampler : public ya::Sampler
{
    std::string _label;
    VkDevice    _device    = VK_NULL_HANDLE;
    const VkAllocationCallbacks* _allocator = nullptr;
    VkSampler   _handle;

    VulkanSampler(const ya::SamplerDesc &ci);
    virtual ~VulkanSampler();

    // Override base class interface - returns void* for platform abstraction
    SamplerHandle getHandle() const override { return SamplerHandle(_handle); }

    // Vulkan-specific typed accessor
    VkSampler getVkHandle() const { return _handle; }
};

} // namespace ya