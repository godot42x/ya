
#pragma once

#include "Core/Base.h"
#include "Render/Core/Sampler.h"
#include "Render/RenderDefines.h"
#include "vulkan/vulkan.h"



struct VulkanSampler : public ya::Sampler
{
    std::string _label;
    VkSampler   _handle;

    VulkanSampler(const ya::SamplerDesc &ci);
    virtual ~VulkanSampler();

    VkSampler getHandle() const { return _handle; }
};