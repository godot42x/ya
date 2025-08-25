
#pragma once


#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Render.h"


struct VulkanRender;
struct VulkanImage;
struct VulkanBuffer;

struct VulkanImage
{

    VulkanRender     *_render      = nullptr;
    VkImage           _handle      = VK_NULL_HANDLE;
    VkDeviceMemory    _imageMemory = VK_NULL_HANDLE;
    VkFormat          _format      = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags _usageFlags  = 0;
    bool              bOwned       = false;

    ya::ImageCreateInfo _ci;


  public:


    VulkanImage() = default;
    virtual ~VulkanImage();

    static std::shared_ptr<VulkanImage> create(VulkanRender *render, const ya::ImageCreateInfo &ci)
    {
        auto ret     = std::make_shared<VulkanImage>();
        ret->_render = render;
        ret->_ci     = ci;

        YA_CORE_ASSERT(ret->allocate(), "Failed to create VulkanImage");

        return ret;
    }
    static std::shared_ptr<VulkanImage> from(VulkanRender *render, VkImage image, VkFormat format, VkImageUsageFlags usages)
    {
        auto ret         = std::make_shared<VulkanImage>();
        ret->_render     = render;
        ret->_handle     = image;
        ret->_format     = format;
        ret->_usageFlags = usages;
        return ret;
    }

    [[nodiscard]] VkImage  getHandle() const { return _handle; }
    [[nodiscard]] VkFormat getFormat() const { return _format; }


  public:
    static void transfer(VkCommandBuffer cmdBuf, VulkanBuffer *srcBuffer, VulkanImage *dstImage);
    static bool transitionLayout(VkCommandBuffer cmdBuf, const VulkanImage *image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout);

  public:
    [[nodiscard]] uint32_t getWidth() const { return static_cast<int>(_ci.extent.width); }
    [[nodiscard]] uint32_t getHeight() const { return static_cast<int>(_ci.extent.height); }

  protected:
    bool allocate();
};
