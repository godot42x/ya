
#pragma once


#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Render/Core/Image.h"
#include "Render/Render.h"

#include "VulkanUtils.h"

#include <unordered_map>

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
    bool              bOwned              = false;
    VkImageLayout     _compatibilityLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    struct SubresourceLayoutKey
    {
        uint32_t aspect = 0;
        uint32_t mip    = 0;
        uint32_t layer  = 0;

        bool operator==(const SubresourceLayoutKey&) const = default;
    };
    struct SubresourceLayoutKeyHash
    {
        size_t operator()(const SubresourceLayoutKey& key) const;
    };
    std::unordered_map<SubresourceLayoutKey, VkImageLayout, SubresourceLayoutKeyHash> _compatibilitySubresourceLayouts;

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
                                              uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                                              EImageLayout::T initialLayout = EImageLayout::Undefined)
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
        ret->_ci.usage       = static_cast<EImageUsage::T>(0);
        if ((usages & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::TransferSrc);
        if ((usages & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::TransferDst);
        if ((usages & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::Sampled);
        if ((usages & VK_IMAGE_USAGE_STORAGE_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::Storage);
        if ((usages & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::ColorAttachment);
        if ((usages & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::DepthStencilAttachment);
        if ((usages & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::TransientAttachment);
        if ((usages & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) != 0)
            ret->_ci.usage = static_cast<EImageUsage::T>(ret->_ci.usage | EImageUsage::InputAttachment);
        ret->_compatibilityLayout = EImageLayout::toVk(initialLayout);
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
    EImageLayout::T              getCompatibilityLayout() const override { return EImageLayout::fromVk(_compatibilityLayout); }
    EImageLayout::T              getCompatibilityLayout(uint32_t aspect, uint32_t mip, uint32_t layer) const override;

    // Vulkan-specific accessors
    [[nodiscard]] VkImage  getVkImage() const { return _handle; }
    [[nodiscard]] VkFormat getVkFormat() const { return _format; }

    void setDebugName(const std::string &name) override;

    void setCompatibilityLayout(EImageLayout::T layout)
    {
      _compatibilityLayout = EImageLayout::toVk(layout);
      _compatibilitySubresourceLayouts.clear();
    }

    void setCompatibilityLayout(EImageLayout::T layout, const ImageSubresourceRange* range);

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
    [[nodiscard]] bool isFullSubresourceRange(const ImageSubresourceRange& range) const;
};
} // namespace ya
