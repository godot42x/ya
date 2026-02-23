#include "VulkanSampler.h"
#include "VulkanRender.h"
#include "core/App/App.h"

namespace ya
{

VulkanSampler::VulkanSampler(const ya::SamplerDesc& ci)
{
    auto vkRender = ya::App::get()->getRender<VulkanRender>();
    vkRender      = static_cast<VulkanRender*>(ya::App::get()->getRender());

    VkSamplerCreateInfo vkCI{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .magFilter               = toVk(ci.magFilter),
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

    VkSamplerCustomBorderColorCreateInfoEXT customBorderInfo{};



    if (ci.borderColor.type == SamplerDesc::EBorderColor::Custom) {
        customBorderInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
        // 自定义 RGBA 边界颜色（示例：半透明绿色）
        customBorderInfo.format                       = VK_FORMAT_R32G32B32A32_SFLOAT; // 颜色格式
        customBorderInfo.customBorderColor.float32[0] = ci.borderColor.color.r;
        customBorderInfo.customBorderColor.float32[1] = ci.borderColor.color.g;
        customBorderInfo.customBorderColor.float32[2] = ci.borderColor.color.b;
        customBorderInfo.customBorderColor.float32[3] = ci.borderColor.color.a;
        vkCI.pNext                                    = &customBorderInfo; // 链接扩展信息
    }
    else {
        switch (ci.borderColor.type) {
        case SamplerDesc::EBorderColor::FloatTransparentBlack:
            vkCI.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            break;
        case SamplerDesc::EBorderColor::IntTransparentBlack:
            vkCI.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
            break;
        case SamplerDesc::EBorderColor::FloatOpaqueBlack:

            vkCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            break;
        case SamplerDesc::EBorderColor::IntOpaqueBlack:
            vkCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            break;
        case SamplerDesc::EBorderColor::FloatOpaqueWhite:
            vkCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            break;
        case SamplerDesc::EBorderColor::IntOpaqueWhite:
            vkCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
            break;
        default:
            UNREACHABLE();
        }
    }

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

// namespace ya
} // namespace ya