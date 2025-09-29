#include "VulkanSampler.h"
#include "VulkanRender.h"
#include "core/App/App.h"


VulkanSampler::VulkanSampler(const ya::SamplerDesc &ci)
{
    using namespace ya;
    auto vkRender = ya::App::get()->getRender<VulkanRender>();
    vkRender      = static_cast<VulkanRender *>(ya::App::get()->getRender());

    VkSamplerCreateInfo vkCI{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .magFilter               = VkFilter::VK_FILTER_LINEAR,
        .minFilter               = toVk(ci.minFilter),
        .mipmapMode              = toVk(ci.mipmapMode),
        .addressModeU            = toVk(ci.addressModeU),
        .addressModeV            = toVk(ci.addressModeV),
        .addressModeW            = toVk(ci.addressModeW),
        .mipLodBias              = ci.mipLodBias,
        .anisotropyEnable        = ci.anisotropyEnable ? VK_TRUE : VK_FALSE,
        .maxAnisotropy           = ci.maxAnisotropy,
        .compareEnable           = ci.compareEnable ? VK_TRUE : VK_FALSE,
        .compareOp               = toVk(ci.compareOp),
        .minLod                  = ci.minLod,
        .maxLod                  = ci.maxLod,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = ci.unnormalizedCoordinates ? VK_TRUE : VK_FALSE,
    };

    if (vkCI.anisotropyEnable == VK_TRUE)
    {
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(vkRender->getPhysicalDevice(), &deviceFeatures);
        if (deviceFeatures.samplerAnisotropy != VK_TRUE)
        {
            YA_CORE_WARN("Anisotropic filtering is not supported by the physical device, disabling it.");
            vkCI.anisotropyEnable = VK_FALSE;
            vkCI.maxAnisotropy    = 1.0f;
        }
    }

    _label = ci.label;
    VK_CALL(vkCreateSampler(vkRender->getDevice(), &vkCI, vkRender->getAllocator(), &_handle));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_SAMPLER, _handle, ci.label.c_str());
    YA_CORE_TRACE("Created sampler {}: {}", ci.label, (uintptr_t)_handle);
    YA_CORE_ASSERT(_handle != VK_NULL_HANDLE, "Failed to create sampler");
}

VulkanSampler::~VulkanSampler()
{
    VK_DESTROY(Sampler, ya::App::get()->getRender<VulkanRender>()->getDevice(), _handle);
}
