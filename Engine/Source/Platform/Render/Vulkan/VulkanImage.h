
#pragma once

#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Render.h"


struct VulkanRender;
struct VulkanImage;

struct VulkanImage
{

    VulkanRender     *_render     = nullptr;
    VkImage           _handle     = VK_NULL_HANDLE;
    VkDeviceMemory    imageMemory = VK_NULL_HANDLE;
    VkFormat          _format     = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags _usageFlags = 0;
    bool              bOwned      = false;

    std::unique_ptr<ImageCreateInfo> _ci = nullptr;


  public:


    VulkanImage() = default;
    virtual ~VulkanImage();

    static std::shared_ptr<VulkanImage> create(VulkanRender *render, const ImageCreateInfo &ci);
    static std::shared_ptr<VulkanImage> from(VulkanRender *render, VkImage image, VkFormat format, VkImageUsageFlags usages);

    [[nodiscard]] VkImage  getHandle() const { return _handle; }
    [[nodiscard]] VkFormat getFormat() const { return _format; }



  protected:
    bool allocate();
};
inline std::shared_ptr<VulkanImage> VulkanImage::create(VulkanRender *render, const ImageCreateInfo &ci)
{
    auto ret     = std::make_shared<VulkanImage>();
    ret->_render = render;
    ret->_ci     = std::make_unique<ImageCreateInfo>(ci);

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
