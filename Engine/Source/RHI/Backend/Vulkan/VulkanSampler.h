
#pragma once

#include "Core/Base.h"
#include "RHI/Core/Sampler.h"
#include "RHI/RenderDefines.h"
#include "vulkan/vulkan.h"

#include "VulkanRender.h"


namespace ya
{

struct VulkanSampler : public ya::Sampler
{
    std::string _label;
    VkDevice    _device    = VK_NULL_HANDLE;
    const VkAllocationCallbacks* _allocator = nullptr;
    VkSampler   _handle;

    VulkanSampler(VulkanRender* render, const ya::SamplerDesc& ci);
    virtual ~VulkanSampler();

    // Override base class interface - returns void* for platform abstraction
    SamplerHandle getHandle() const override { return SamplerHandle(_handle); }

    // Vulkan-specific typed accessor
    VkSampler getVkHandle() const { return _handle; }
};

} // namespace ya
