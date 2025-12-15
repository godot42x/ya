#include "Texture.h"

#include "Core/App/App.h"
#include "Core/FileSystem/FileSystem.h"
#include "Render/Render.h"
#include "stb/stb_image.h"
#include <cstddef>

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include <vulkan/vulkan_core.h>



namespace ya

{

Texture::Texture(const std::string &filepath)
{

    int   texWidth = -1, texHeight = -1, texChannels = -1;
    void *pixels = nullptr;

    // FileSystem::get()->getPluginRoots()
    pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        YA_CORE_ERROR("failed to load texture image! {}", filepath.data());
        return;
    }

    _filepath = filepath;
    _channels = 4; // Force RGBA
    createImage(pixels, (uint32_t)texWidth, (uint32_t)texHeight, EFormat::R8G8B8A8_UNORM);
    stbi_image_free(pixels);
    YA_CORE_TRACE("Created texture from file: {} ({}x{}, {} channels)", filepath.data(), texWidth, texHeight, texChannels);
}

Texture::Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data)
{
    YA_CORE_ASSERT((uint32_t)data.size() == width * height, "pixel data size does not match width * height");
    _channels = 4;
    createImage(data.data(), width, height, EFormat::R8G8B8A8_UNORM);
    YA_CORE_TRACE("Created texture from data({}x{})", width, height);
}

Texture::Texture(uint32_t width, uint32_t height, const void *data, size_t dataSize, EFormat::T format)
{
    _format = format;
    // Derive channels from format
    switch (format) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::B8G8R8A8_UNORM:
        _channels = 4;
        break;
    default:
        _channels = 4; // Default
        break;
    }

    createImage(data, width, height, format);
    YA_CORE_TRACE("Created texture from raw data ({}x{}, format: {})", width, height, (int)format);
}

// Platform-independent accessor
FormatHandle Texture::getFormatHandle() const
{
    // For now, we'll store the VkFormat in the FormatHandle
    // In a more complete abstraction, you'd have a Format enum or interface
    return FormatHandle{reinterpret_cast<void *>(static_cast<std::uintptr_t>(toVk(_format)))};
}

void Texture::setLabel(const std::string &label)
{
    _label = label;
    image->setDebugName(std::format("Texture_Image_{}", label));
    imageView->setDebugName(std::format("Texture_ImageView_{}", label));
}

void Texture::createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight, EFormat::T format)
{
    _width  = texWidth;
    _height = texHeight;
    _format = format;

    auto vkRender = ya::App::get()->getRender<VulkanRender>();

    // Calculate image size based on format
    size_t pixelSize = 4; // Default RGBA
    switch (format) {
    case EFormat::B8G8R8A8_UNORM:
        pixelSize = 4;
        break;
    default:
        pixelSize = 4;
        break;
    }

    VkDeviceSize imageSize = pixelSize * texWidth * texHeight;

    // Create Vulkan image (will be stored as IImage*)
    auto vkImage = VulkanImage::create(
        vkRender,
        ya::ImageCreateInfo{
            .label  = std::format("Texture_Image_{}", _label),
            .format = format,
            .extent = {
                .width  = texWidth,
                .height = texHeight,
                .depth  = 1,
            },
            .mipLevels     = 1,
            .samples       = ESampleCount::Sample_1,
            .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
            .sharingMode   = ESharingMode::Exclusive,
            .initialLayout = EImageLayout::Undefined,
        });

    // Store as abstract interface
    image     = vkImage;
    imageView = std::make_shared<VulkanImageView>(vkRender, vkImage.get(), VK_IMAGE_ASPECT_COLOR_BIT);

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        vkRender,
        ya::BufferCreateInfo{
            .label         = std::format("StagingBuffer_Texture_{}", _filepath),
            .usage         = EBufferUsage::TransferSrc, // from buffer to image
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(imageSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    // TODO: Abstract this - Texture should not directly use Vulkan types
    auto           *cmdBuf   = vkRender->beginIsolateCommands();
    VkCommandBuffer vkCmdBuf = cmdBuf->getHandleAs<VkCommandBuffer>();

    // Do layout transition image: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY
    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanImage::transfer(vkCmdBuf, dynamic_cast<VulkanBuffer *>(stagingBuffer.get()), vkImage.get());
    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkRender->endIsolateCommands(cmdBuf);

    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, vkImage->getHandle().as<VkImage>(), std::format("Texture_Image_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, vkImage->_imageMemory, std::format("Texture_ImageMemory_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, dynamic_cast<VulkanImageView *>(imageView.get())->getHandle().as<VkImageView>(), std::format("Texture_ImageView_{}", _filepath));
}

} // namespace ya
