
#pragma once

#include "Foundation/Core/Base.h"
#include "Foundation/RHI/Core/Sampler.h"
#include "Foundation/RHI/RenderDefines.h"
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

    /// Destroy the platform sampler immediately (used at device teardown so
    /// the handle is released while the device is still alive, regardless of
    /// how many shared_ptr owners outlive the render device).
    void destroyNow();

    // Override base class interface - returns void* for platform abstraction
    SamplerHandle getHandle() const override { return SamplerHandle(_handle); }

    // Vulkan-specific typed accessor
    VkSampler getVkHandle() const { return _handle; }
};

} // namespace ya
