
#pragma once


#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Core/Image.h"
#include "Render/Render.h"


namespace ya
{
struct VulkanRender;
struct VulkanImage;
struct VulkanBuffer;

struct VulkanImage : public IImage
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

    // IImage interface
    [[nodiscard]] ImageHandle    getHandle() const override { return ImageHandle{_handle}; }
    [[nodiscard]] uint32_t       getWidth() const override { return static_cast<uint32_t>(_ci.extent.width); }
    [[nodiscard]] uint32_t       getHeight() const override { return static_cast<uint32_t>(_ci.extent.height); }
    [[nodiscard]] EFormat::T     getFormat() const override { return _ci.format; }
    [[nodiscard]] EImageUsage::T getUsage() const override { return _ci.usage; }

    // Vulkan-specific accessors
    [[nodiscard]] VkImage  getVkImage() const { return _handle; }
    [[nodiscard]] VkFormat getVkFormat() const { return _format; }

    void setDebugName(const std::string &name) override;


  public:
    static void transfer(VkCommandBuffer cmdBuf, VulkanBuffer *srcBuffer, VulkanImage *dstImage);
    static bool transitionLayout(VkCommandBuffer cmdBuf, const VulkanImage *image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout);


  protected:
    bool allocate();
};
} // namespace ya
