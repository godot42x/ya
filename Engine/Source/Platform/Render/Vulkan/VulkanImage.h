
#pragma once


#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Core/Image.h"
#include "Render/Render.h"

#include "VulkanUtils.h"

// VMA forward declaration (full definition comes from vk_mem_alloc.h via VulkanRender.h)
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace ya
{
struct VulkanRender;
struct VulkanImage;
struct VulkanBuffer;

struct VulkanImage : public IImage
{

    VulkanRender     *_render      = nullptr;
    VkImage           _handle      = VK_NULL_HANDLE;
    VmaAllocation     _allocation  = VK_NULL_HANDLE;
    VkFormat          _format      = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags _usageFlags  = 0;
    bool              bOwned       = false;
    // TODO: layout should be sync in: different queues, different cmdbuf...
    VkImageLayout     _layout      = VK_IMAGE_LAYOUT_UNDEFINED;

    ya::ImageCreateInfo _ci;

  public:


    VulkanImage() = default;
    virtual ~VulkanImage();

    static std::shared_ptr<VulkanImage> create(VulkanRender *render, const ya::ImageCreateInfo &ci)
    {
        auto ret     = std::make_shared<VulkanImage>();
        ret->_render = render;
        ret->_ci     = ci;

        bool ok = ret->allocate();
        if (!ok) {
            YA_CORE_ERROR("Failed to allocate VulkanImage");
            return nullptr;
        }
        ret->setDebugName(ci.label);


        return ret;
    }
    static std::shared_ptr<VulkanImage> from(VulkanRender *render, VkImage image, VkFormat format, VkImageUsageFlags usages,
                                              uint32_t width = 0, uint32_t height = 0,
                                              uint32_t mipLevels = 1, uint32_t arrayLayers = 1)
    {
        auto ret         = std::make_shared<VulkanImage>();
        ret->_render     = render;
        ret->_handle     = image;
        ret->_format     = format;
        ret->_usageFlags = usages;
        ret->_ci.format      = EFormat::fromVk(format);
        ret->_ci.extent      = {width, height, 1};
        ret->_ci.mipLevels   = mipLevels;
        ret->_ci.arrayLayers = arrayLayers;
        return ret;
    }

    // IImage interface
    [[nodiscard]] ImageHandle    getHandle() const override { return ImageHandle{_handle}; }
    [[nodiscard]] uint32_t       getWidth() const override { return static_cast<uint32_t>(_ci.extent.width); }
    [[nodiscard]] uint32_t       getHeight() const override { return static_cast<uint32_t>(_ci.extent.height); }
    [[nodiscard]] EFormat::T     getFormat() const override { return _ci.format; }
    [[nodiscard]] EImageUsage::T getUsage() const override { return _ci.usage; }
    [[nodiscard]] uint32_t       getMipLevels() const override { return _ci.mipLevels; }
    [[nodiscard]] uint32_t       getArrayLayers() const override { return _ci.arrayLayers; }
    EImageLayout::T              getLayout() const override { return EImageLayout::fromVk(_layout); }

    // Vulkan-specific accessors
    [[nodiscard]] VkImage  getVkImage() const { return _handle; }
    [[nodiscard]] VkFormat getVkFormat() const { return _format; }

    void setDebugName(const std::string &name) override;

    void setLayout(EImageLayout::T layout)
    {
      _layout = EImageLayout::toVk(layout);
    }

    struct LayoutTransition
    {
      VulkanImage         *image     = nullptr;
      EImageLayout::T      newLayout = EImageLayout::Undefined;
      ImageSubresourceRange range    = {};
      bool                 useRange = false;

      LayoutTransition(
        VulkanImage            *inImage = nullptr,
        EImageLayout::T         inLayout = EImageLayout::Undefined,
        const ImageSubresourceRange *inRange = nullptr)
        : image(inImage),
          newLayout(inLayout)
      {
        if (inRange) {
          range    = *inRange;
          useRange = true;
        }
      }
    };

    static bool transitionLayouts(VkCommandBuffer cmdBuf, const std::vector<LayoutTransition> &transitions);

    bool isValid() const { return _handle != VK_NULL_HANDLE && _allocation != VK_NULL_HANDLE; }


  public:
    static void transfer(VkCommandBuffer cmdBuf, VulkanBuffer *srcBuffer, VulkanImage *dstImage);
    static bool transitionLayout(VkCommandBuffer cmdBuf, VulkanImage *const image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 const VkImageSubresourceRange *subresourceRange = nullptr);


  protected:
    bool allocate();
};
} // namespace ya
