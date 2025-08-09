
#pragma once

#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Render.h"


struct VulkanRender;
struct VulkanImage;

struct VulkanImage
{
    struct CreateInfo
    {
        EFormat::T format = EFormat::Undefined;
        struct
        {
            uint32_t width  = 0;
            uint32_t height = 0;
            uint32_t depth  = 1;
        } extent;
        uint32_t        mipLevels             = 1;
        ESampleCount::T samples               = ESampleCount::Sample_1;
        EImageUsage::T  usage                 = EImageUsage::Sampled | EImageUsage::TransferDst;
        ESharingMode::T sharingMode           = {};
        uint32_t        queueFamilyIndexCount = 0;
        const uint32_t *pQueueFamilyIndices   = nullptr;
        EImageLayout::T initialLayout         = EImageLayout::Undefined;
        // TODO: manual conversion
        // EImageLayout::T finalLayout           = EImageLayout::ShaderReadOnlyOptimal;
    };


    VulkanRender               *_render     = nullptr;
    VkImage                     _handle     = VK_NULL_HANDLE;
    VkDeviceMemory              imageMemory = VK_NULL_HANDLE;
    VkFormat                    _format     = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags           _usageFlags = 0;
    std::unique_ptr<CreateInfo> _ci         = nullptr;
    bool                        bOwned      = false;

  public:


    VulkanImage() = default;
    virtual ~VulkanImage();

    static std::shared_ptr<VulkanImage> create(VulkanRender *render, const CreateInfo &ci);
    static std::shared_ptr<VulkanImage> from(VulkanRender *render, VkImage image, VkFormat format, VkImageUsageFlags usages);

    [[nodiscard]] VkImage  getHandle() const { return _handle; }
    [[nodiscard]] VkFormat getFormat() const { return _format; }



  protected:
    bool allocate();
};
inline std::shared_ptr<VulkanImage> VulkanImage::create(VulkanRender *render, const CreateInfo &ci)
{
    auto ret     = std::make_shared<VulkanImage>();
    ret->_render = render;
    ret->_ci     = std::make_unique<CreateInfo>(ci);

    NE_CORE_ASSERT(ret->allocate(), "Failed to create VulkanImage");

    return ret;
}

inline std::shared_ptr<VulkanImage> VulkanImage::from(VulkanRender *render, VkImage image, VkFormat format, VkImageUsageFlags usages)
{
    auto ret         = std::make_shared<VulkanImage>();
    ret->_render     = render;
    ret->_handle     = image;
    ret->_format     = format;
    ret->_usageFlags = usages;
    return ret;
}
