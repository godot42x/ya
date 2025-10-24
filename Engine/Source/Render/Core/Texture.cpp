#include "Texture.h"

#include "Core/App/App.h"
#include "Core/FileSystem/FileSystem.h"
#include "stb/stb_image.h"
#include <cstddef>

#include <vulkan/vulkan_core.h>
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"



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

    createImage(pixels, (uint32_t)texWidth, (uint32_t)texHeight);
    stbi_image_free(pixels);
    YA_CORE_TRACE("Crate texture from file: {} ({}x{}, {} channels)", filepath.data(), texWidth, texHeight, texChannels);
}

Texture::Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data)
{
    YA_CORE_ASSERT((uint32_t)data.size() == width * height, "pixel data size does not match width * height");
    createImage(data.data(), width, height);
    YA_CORE_TRACE("Created texture from data({}x{})", width, height);
}

// Platform-independent accessor
FormatHandle Texture::getFormatHandle() const
{
    // For now, we'll store the VkFormat in the FormatHandle
    // In a more complete abstraction, you'd have a Format enum or interface
    return FormatHandle{reinterpret_cast<void *>(static_cast<std::uintptr_t>(toVk(_format)))};
}

void Texture::createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight)
{
    _width  = texWidth;
    _height = texHeight;

    auto         vkRender  = ya::App::get()->getRender<VulkanRender>();
    VkDeviceSize imageSize = sizeof(uint8_t) * texWidth * texHeight * 4;

    // Create Vulkan image (will be stored as IImage*)
    auto vkImage = VulkanImage::create(
        vkRender,
        ya::ImageCreateInfo{
            .format = EFormat::R8G8B8A8_UNORM,
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
            .usage         = EBufferUsage::TransferSrc, // from buffer to image
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(imageSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            .label         = std::format("StagingBuffer_Texture_{}", _filepath),
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
