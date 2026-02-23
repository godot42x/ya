#include "VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"

namespace ya
{

// Helper function to get the correct aspect mask based on image format
static VkImageAspectFlags getAspectMask(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return VK_IMAGE_ASPECT_DEPTH_BIT;

    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;

    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;


    default:
        // UNREACHABLE();
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static bool getAccessMask(VkImageLayout layout, VkAccessFlags& mask, bool isSrc)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        mask = 0;
        return true;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        mask = VK_ACCESS_HOST_WRITE_BIT;
        return true;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        return true;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        return true;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        mask = VK_ACCESS_TRANSFER_READ_BIT;
        return true;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        mask = VK_ACCESS_TRANSFER_WRITE_BIT;
        return true;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        mask = VK_ACCESS_SHADER_READ_BIT;
        return true;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        mask = isSrc ? VK_ACCESS_MEMORY_READ_BIT : 0;
        return true;
    default:
        return false;
    }
}

static VkPipelineStageFlags getStageMask(VkImageLayout layout, VkAccessFlags accessMask, bool isSrc)
{
    VkPipelineStageFlags stages = 0;
    if (accessMask & VK_ACCESS_HOST_WRITE_BIT) {
        stages |= VK_PIPELINE_STAGE_HOST_BIT;
    }
    if (accessMask & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) {
        stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (accessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) {
        stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (accessMask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
        stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    if (accessMask & VK_ACCESS_SHADER_READ_BIT) {
        stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        stages |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    if (stages == 0) {
        return isSrc ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    return stages;
}

VulkanImage::~VulkanImage()
{
    if (bOwned) {
        VK_DESTROY(Image, _render->getDevice(), _handle);
        VK_FREE(Memory, _render->getDevice(), _imageMemory);
    }
}

void VulkanImage::setDebugName(const std::string& name)
{
    _render->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, (void*)_handle, name);
    _render->setDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, (void*)_imageMemory, name + "_Memory");
}

void VulkanImage::transfer(VkCommandBuffer cmdBuf, VulkanBuffer* srcBuffer, VulkanImage* dstImage)
{

    VkBufferImageCopy copyRegion{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = VkImageSubresourceLayers{
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .mipLevel       = 0,
             .baseArrayLayer = 0,
             .layerCount     = 1,
        },
        .imageOffset = VkOffset3D{0, 0, 0},
        .imageExtent = VkExtent3D{
            .width  = dstImage->getWidth(),
            .height = dstImage->getHeight(),
            .depth  = 1,
        },
    };
    vkCmdCopyBufferToImage(cmdBuf,
                           srcBuffer->getHandleAs<VkBuffer>(),
                           dstImage->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copyRegion);
}

bool VulkanImage::transitionLayout(VkCommandBuffer cmdBuf, VulkanImage* const image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout,
                                   const VkImageSubresourceRange* subresourceRange)
{
    if (image == VK_NULL_HANDLE) {
        YA_CORE_ERROR("VulkanImage::transitionImageLayout image is null");
        return false;
    }
    if (newLayout == oldLayout) {
        return true;
    }
    YA_ASSERT(image->_layout == oldLayout, "VulkanImage::transitionImageLayout image layout is not equal to old layout");

    VkImageMemoryBarrier imb{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = {},
        .dstAccessMask       = {},
        .oldLayout           = oldLayout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image->getVkImage(),
        .subresourceRange    = {
               .aspectMask     = getAspectMask(image->_format),
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1,
        },
    };
    if (subresourceRange) {
        imb.subresourceRange = *subresourceRange;
    }

    if (!getAccessMask(oldLayout, imb.srcAccessMask, true) || !getAccessMask(newLayout, imb.dstAccessMask, false)) {
        YA_ERROR("Unsupported layout transition: {}->{}", std::to_string(oldLayout), std::to_string(newLayout));
        UNREACHABLE();
        return false;
    }

    VkPipelineStageFlags srcStage  = getStageMask(oldLayout, imb.srcAccessMask, true);
    VkPipelineStageFlags destStage = getStageMask(newLayout, imb.dstAccessMask, false);


    vkCmdPipelineBarrier(cmdBuf,
                         srcStage,
                         destStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &imb);

    // BUG: this real format changed later  after cmd execution, now it's invalid
    image->_layout = newLayout;

    return true;
}

bool VulkanImage::transitionLayouts(VkCommandBuffer cmdBuf, const std::vector<LayoutTransition>& transitions)
{
    if (transitions.empty()) {
        return true;
    }

    std::vector<VkImageMemoryBarrier> barriers;
    VkPipelineStageFlags              srcStages = 0;
    VkPipelineStageFlags              dstStages = 0;

    for (const auto& transition : transitions) {
        if (!transition.image) {
            continue;
        }
        auto oldLayout = transition.image->getLayout();
        auto newLayout = transition.newLayout;
        if (oldLayout == newLayout) {
            continue;
        }

        VkImageMemoryBarrier barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = 0,
            .dstAccessMask       = 0,
            .oldLayout           = EImageLayout::toVk(oldLayout),
            .newLayout           = EImageLayout::toVk(newLayout),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = transition.image->getVkImage(),
            .subresourceRange    = {},
        };

        barrier.subresourceRange.aspectMask     = getAspectMask(transition.image->getVkFormat());
        barrier.subresourceRange.baseMipLevel   = transition.range.baseMipLevel;
        barrier.subresourceRange.levelCount     = transition.range.levelCount;
        barrier.subresourceRange.baseArrayLayer = transition.range.baseArrayLayer;
        barrier.subresourceRange.layerCount     = transition.range.layerCount;

        if (transition.useRange) {
            barrier.subresourceRange.aspectMask = transition.range.aspectMask;
        }

        if (!getAccessMask(barrier.oldLayout, barrier.srcAccessMask, true) || !getAccessMask(barrier.newLayout, barrier.dstAccessMask, false)) {
            YA_ERROR("Unsupported layout transition: {}->{}", std::to_string(barrier.oldLayout), std::to_string(barrier.newLayout));
            UNREACHABLE();
            continue;
        }

        srcStages |= getStageMask(barrier.oldLayout, barrier.srcAccessMask, true);
        dstStages |= getStageMask(barrier.newLayout, barrier.dstAccessMask, false);
        barriers.push_back(barrier);
        transition.image->setLayout(newLayout);
    }

    if (barriers.empty()) {
        return true;
    }

    if (srcStages == 0) {
        srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (dstStages == 0) {
        dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmdBuf,
                         srcStages,
                         dstStages,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         static_cast<uint32_t>(barriers.size()),
                         barriers.data());

    return true;
}

// Helper function to check if a format is supported by the device
static bool isFormatSupported(VulkanRender* vkRender, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage)
{
    VkPhysicalDevice physicalDevice = vkRender->getPhysicalDevice();

    VkImageFormatProperties2 formatProperties = {};
    formatProperties.sType                    = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

    VkPhysicalDeviceImageFormatInfo2 formatInfo = {};
    formatInfo.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    formatInfo.format                           = format;
    formatInfo.type                             = VK_IMAGE_TYPE_2D;
    formatInfo.tiling                           = tiling;
    formatInfo.usage                            = usage;
    formatInfo.flags                            = 0;

    VkResult result = vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &formatInfo, &formatProperties);
    return result == VK_SUCCESS;
}

bool VulkanImage::allocate()
{
    _format     = toVk(_ci.format);
    _usageFlags = toVk(_ci.usage);

    // Check format support: prefer OPTIMAL tiling (better performance and wider support)
    bool bSupportsOptimal = isFormatSupported(_render, _format, VK_IMAGE_TILING_OPTIMAL, _usageFlags);
    bool bSupportsLinear  = false;

    VkImageTiling selectedTiling = VK_IMAGE_TILING_OPTIMAL;

    if (!bSupportsOptimal)
    {
        // Fallback to LINEAR if OPTIMAL is not supported
        bSupportsLinear = isFormatSupported(_render, _format, VK_IMAGE_TILING_LINEAR, _usageFlags);
        if (bSupportsLinear)
        {
            selectedTiling = VK_IMAGE_TILING_LINEAR;
            YA_CORE_WARN("VulkanImage::allocate format {} does not support OPTIMAL tiling, using LINEAR", std::to_string(_format));
        }
        else
        {
            YA_CORE_ERROR("VulkanImage::allocate format {} is not supported by the device (neither OPTIMAL nor LINEAR)", std::to_string(_format));
            return false;
        }
    }

    // Convert EImageCreateFlag to VkImageCreateFlags, excluding sparse flags
    VkImageCreateFlags vkFlags = 0;
    if (_ci.flags & EImageCreateFlag::CubeCompatible)
        vkFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (_ci.flags & EImageCreateFlag::MutableFormat)
        vkFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    // Note: Sparse flags are intentionally not mapped as they require device feature support
    if (_ci.flags & EImageCreateFlag::Protected)
        vkFlags |= VK_IMAGE_CREATE_PROTECTED_BIT;
    if (_ci.flags & EImageCreateFlag::ExtendedUsage)
        vkFlags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    if (_ci.flags & EImageCreateFlag::Disjoint)
        vkFlags |= VK_IMAGE_CREATE_DISJOINT_BIT;

    bool     bSameQueueFamily     = _render->isGraphicsPresentSameQueueFamily();
    auto     sharingMode          = bSameQueueFamily ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    uint32_t queueFamilyCont      = bSameQueueFamily ? 0 : 2;
    uint32_t queueFamilyIndices[] = {
        (uint32_t)_render->getGraphicsQueueFamilyInfo().queueFamilyIndex,
        (uint32_t)_render->getPresentQueueFamilyInfo().queueFamilyIndex,
    };


    VkImageCreateInfo imageCreateInfo{
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext     = nullptr,
        .flags     = vkFlags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = _format,
        .extent    = {
               .width  = _ci.extent.width,
               .height = _ci.extent.height,
               .depth  = _ci.extent.depth,
        },
        .mipLevels             = _ci.mipLevels,
        .arrayLayers           = _ci.arrayLayers,
        .samples               = toVk(_ci.samples),
        .tiling                = selectedTiling,
        .usage                 = _usageFlags,
        .sharingMode           = sharingMode,
        .queueFamilyIndexCount = queueFamilyCont,
        .pQueueFamilyIndices   = queueFamilyIndices,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VK_CALL(vkCreateImage(_render->getDevice(), &imageCreateInfo, _render->getAllocator(), &_handle));


    // allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(_render->getDevice(), _handle, &memRequirements);
    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = static_cast<uint32_t>(_render->getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memRequirements.memoryTypeBits)),
    };

    VK_CALL(vkAllocateMemory(_render->getDevice(), &allocInfo, nullptr, &_imageMemory));
    VK_CALL(vkBindImageMemory(_render->getDevice(), _handle, _imageMemory, 0));


    _layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (_ci.initialLayout != EImageLayout::Undefined) {
        auto cmdBuf = _render->beginIsolateCommands("ImageInitialLayout");
        cmdBuf->transitionImageLayout(this, EImageLayout::Undefined, _ci.initialLayout);
        _render->endIsolateCommands(cmdBuf);
    }
    bOwned = true;
    return true;
}

} // namespace ya