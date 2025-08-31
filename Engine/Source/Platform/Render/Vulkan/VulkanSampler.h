
#pragma once

#include "Core/Base.h"
#include "Render/RenderDefines.h"
#include "vulkan/vulkan.h"


struct VulkanRender;

struct VulkanSampler
{
    VulkanRender *_render;
    std::string   _name;
    VkSampler     _handle;

    // VkFilter             _filter;
    // VkSamplerAddressMode _addressModeU;

    VulkanSampler(VulkanRender *render, const ya::SamplerCreateInfo &ci);
    virtual ~VulkanSampler();

    VkSampler getHandle() const { return _handle; }
};